#include "FasterWhisperBackend.h"

#include "util/PythonEnv.h"

#include <QByteArray>
#include <QDir>
#include <QDeadlineTimer>
#include <QJsonParseError>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

namespace {
// Locate the sidecar: installed data dir first, then next to the source tree.
QString sidecarPath() {
    const QStringList candidates = {
        QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
            .filePath(QStringLiteral("scrybe/backends/faster_whisper_sidecar.py")),
        QStringLiteral("/usr/local/share/scrybe/faster_whisper_sidecar.py"),
    };
    for (const QString &c : candidates)
        if (QFileInfo::exists(c))
            return c;
    return candidates.first();
}
} // namespace

FasterWhisperBackend::FasterWhisperBackend(const QString &scriptOverride, int requestTimeoutMs,
                                           const QString &pythonOverride)
    : m_scriptOverride(scriptOverride), m_pythonOverride(pythonOverride),
      m_requestTimeoutMs(requestTimeoutMs) {}
FasterWhisperBackend::~FasterWhisperBackend() { unload(); }

QString FasterWhisperBackend::readLine(int timeoutMs, QString *err) {
    // One total deadline: partial stdout must not reset the request timeout.
    QDeadlineTimer deadline(timeoutMs);
    while (!m_proc->canReadLine()) {
        if (m_proc->state() == QProcess::NotRunning) {
            *err = QStringLiteral("sidecar exited: %1")
                       .arg(QString::fromUtf8(m_proc->readAllStandardError()));
            return QString();
        }
        if (deadline.hasExpired() || !m_proc->waitForReadyRead(int(deadline.remainingTime()))) {
            *err = QStringLiteral("sidecar timed out.");
            return QString();
        }
    }
    return QString::fromUtf8(m_proc->readLine()).trimmed();
}

bool FasterWhisperBackend::load(const QString &model, const QString &device,
                                QString *effectiveDevice, QString *err) {
    unload();

    const QString script = m_scriptOverride.isEmpty() ? sidecarPath() : m_scriptOverride;
    if (!QFileInfo::exists(script)) {
        *err = QStringLiteral("faster-whisper sidecar not found at %1.").arg(script);
        return false;
    }

    // Normalise device to cuda|cpu|auto (accepts OpenVINO-style strings too).
    QString dev = device.toLower();
    if (dev.contains("cuda") || dev.contains("gpu")) dev = QStringLiteral("cuda");
    else if (dev.contains("cpu")) dev = QStringLiteral("cpu");
    else dev = QStringLiteral("auto");

    m_proc = new QProcess();
    m_proc->setProgram(m_pythonOverride.isEmpty() ? scrybe::pythonExecutable() : m_pythonOverride);
    m_proc->setArguments({script, QStringLiteral("--model"), model,
                          QStringLiteral("--device"), dev});
    m_proc->start();
    if (!m_proc->waitForStarted(5000)) {
        *err = QStringLiteral("could not start python3 sidecar.");
        unload();
        return false;
    }

    const QString line = readLine(120000, err); // model download can be slow
    if (line.isEmpty()) { unload(); return false; }

    const QJsonObject obj = QJsonDocument::fromJson(line.toUtf8()).object();
    if (obj.value(QStringLiteral("status")).toString() != QLatin1String("ready")) {
        *err = obj.value(QStringLiteral("error")).toString(
            QStringLiteral("sidecar failed to start."));
        unload();
        return false;
    }
    *effectiveDevice = obj.value(QStringLiteral("device")).toString(dev);
    m_loadedModel = model;
    m_loadedDevice = device;
    return true;
}

void FasterWhisperBackend::unload() {
    m_loadedModel.clear();
    m_loadedDevice.clear();
    stopProcess();
}

void FasterWhisperBackend::stopProcess() {
    if (!m_proc)
        return;
    m_proc->closeWriteChannel();
    m_proc->terminate();
    if (!m_proc->waitForFinished(2000))
        m_proc->kill();
    delete m_proc;
    m_proc = nullptr;
}

bool FasterWhisperBackend::transcribe(const std::vector<float> &pcm16k,
                                      const QString &language, QString *text,
                                      QString *err) {
    if (!m_proc || m_proc->state() == QProcess::NotRunning) {
        if (m_loadedModel.isEmpty()) {
            *err = QStringLiteral("faster-whisper sidecar is not running.");
            return false;
        }
        // A timed-out process has been discarded; start a fresh protocol
        // stream before accepting another request.
        const QString model = m_loadedModel, device = m_loadedDevice;
        QString effective;
        if (!load(model, device, &effective, err)) {
            // Explicit load/unload clear residency, but a transient automatic
            // restart failure must retain the last successful configuration
            // so a later preview/final can retry a fresh process.
            m_loadedModel = model;
            m_loadedDevice = device;
            return false;
        }
    }
    // Header line with the payload size, then the raw float32 samples (avoids
    // the +33% base64 overhead and an extra copy on both sides).
    const QByteArray raw(reinterpret_cast<const char *>(pcm16k.data()),
                         int(pcm16k.size() * sizeof(float)));
    const QJsonObject req{
        {QStringLiteral("n_bytes"), int(raw.size())},
        {QStringLiteral("language"), language.isEmpty() ? QStringLiteral("auto")
                                                        : language},
    };
    QDeadlineTimer deadline(m_requestTimeoutMs);
    const QByteArray header = QJsonDocument(req).toJson(QJsonDocument::Compact) + '\n';
    if (m_proc->write(header) != header.size() || m_proc->write(raw) != raw.size()) {
        *err = QStringLiteral("could not send audio to the sidecar.");
        stopProcess();
        return false;
    }
    while (m_proc->bytesToWrite() > 0) {
        if (deadline.hasExpired() || !m_proc->waitForBytesWritten(int(deadline.remainingTime()))) {
            *err = QStringLiteral("sidecar timed out while receiving audio.");
            stopProcess();
            return false;
        }
    }

    const QString line = readLine(int(deadline.remainingTime()), err);
    if (line.isEmpty()) {
        stopProcess(); // never let a late response satisfy the next request
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(line.toUtf8(), &parseError);
    const QJsonObject obj = document.object();
    if (parseError.error != QJsonParseError::NoError || !document.isObject() ||
        (!obj.contains(QStringLiteral("error")) && !obj.value(QStringLiteral("text")).isString())) {
        *err = QStringLiteral("sidecar returned an invalid transcription response.");
        stopProcess();
        return false;
    }
    if (obj.contains(QStringLiteral("error"))) {
        *err = obj.value(QStringLiteral("error")).toString();
        return false;
    }
    *text = obj.value(QStringLiteral("text")).toString().trimmed();
    return true;
}
