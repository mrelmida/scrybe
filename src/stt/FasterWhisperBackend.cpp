#include "FasterWhisperBackend.h"

#include <QByteArray>
#include <QDir>
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

FasterWhisperBackend::FasterWhisperBackend() = default;
FasterWhisperBackend::~FasterWhisperBackend() { unload(); }

QString FasterWhisperBackend::readLine(int timeoutMs, QString *err) {
    // Accumulate stdout until a newline (the sidecar writes one JSON per line).
    while (!m_proc->canReadLine()) {
        if (m_proc->state() == QProcess::NotRunning) {
            *err = QStringLiteral("sidecar exited: %1")
                       .arg(QString::fromUtf8(m_proc->readAllStandardError()));
            return QString();
        }
        if (!m_proc->waitForReadyRead(timeoutMs)) {
            *err = QStringLiteral("sidecar timed out.");
            return QString();
        }
    }
    return QString::fromUtf8(m_proc->readLine()).trimmed();
}

bool FasterWhisperBackend::load(const QString &model, const QString &device,
                                QString *effectiveDevice, QString *err) {
    unload();

    const QString script = sidecarPath();
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
    m_proc->setProgram(QStringLiteral("python3"));
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
    return true;
}

void FasterWhisperBackend::unload() {
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
        *err = QStringLiteral("faster-whisper sidecar is not running.");
        return false;
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
    m_proc->write(QJsonDocument(req).toJson(QJsonDocument::Compact) + '\n');
    m_proc->write(raw);
    m_proc->waitForBytesWritten(5000);

    const QString line = readLine(120000, err);
    if (line.isEmpty())
        return false;
    const QJsonObject obj = QJsonDocument::fromJson(line.toUtf8()).object();
    if (obj.contains(QStringLiteral("error"))) {
        *err = obj.value(QStringLiteral("error")).toString();
        return false;
    }
    *text = obj.value(QStringLiteral("text")).toString().trimmed();
    return true;
}
