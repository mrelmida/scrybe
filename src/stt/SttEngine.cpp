#include "SttEngine.h"

#include "SttBackend.h"

#include <QtGlobal>

#include <cmath>
#include <vector>

namespace {
// Linear resample to 16 kHz mono float (Whisper's required input rate).
std::vector<float> toWhisperInput(const QVector<float> &pcm, int srcRate) {
    std::vector<float> out;
    if (pcm.isEmpty())
        return out;
    if (srcRate == 16000 || srcRate <= 0) {
        out.assign(pcm.begin(), pcm.end());
        return out;
    }
    const double ratio = 16000.0 / double(srcRate);
    const auto dstN = qint64(pcm.size() * ratio);
    out.reserve(dstN);
    for (qint64 i = 0; i < dstN; ++i) {
        const double srcPos = i / ratio;
        const auto i0 = qint64(srcPos);
        const auto i1 = qMin<qint64>(i0 + 1, pcm.size() - 1);
        const double frac = srcPos - i0;
        out.push_back(float(pcm[i0] * (1.0 - frac) + pcm[i1] * frac));
    }
    return out;
}
} // namespace

// ---------------------------------------------------------------------------- //
// SttWorker (runs on the worker thread)
// ---------------------------------------------------------------------------- //
SttWorker::SttWorker(QObject *parent) : QObject(parent) {}
SttWorker::~SttWorker() = default;

void SttWorker::doLoad(const QString &backend, const QString &model,
                       const QString &device) {
    if (!m_backend || m_backendType != backend) {
        m_backend = makeSttBackend(backend);
        m_backendType = backend;
    }
    if (!m_backend) {
        emit failed(QStringLiteral("Unknown STT backend '%1'.").arg(backend));
        return;
    }
    QString effectiveDevice, err;
    if (m_backend->load(model, device, &effectiveDevice, &err))
        emit loaded(effectiveDevice);
    else
        emit failed(err);
}

void SttWorker::doUnload() {
    if (m_backend) {
        m_backend->unload();
        emit unloaded();
    }
}

void SttWorker::doTranscribe(const QVector<float> &pcm, int sampleRate,
                             const QString &language, bool isFinal) {
    if (!m_backend) {
        if (isFinal)
            emit failed(QStringLiteral("STT backend is not loaded yet."));
        return;
    }
    std::vector<float> audio = toWhisperInput(pcm, sampleRate);
    if (audio.empty()) {
        emit result(QString(), QString(), isFinal);
        return;
    }
    QString text, err;
    if (m_backend->transcribe(audio, language, &text, &err))
        emit result(text, QString(), isFinal);
    else if (isFinal)
        emit failed(err);
}

// ---------------------------------------------------------------------------- //
// SttEngine (GUI-thread facade)
// ---------------------------------------------------------------------------- //
SttEngine::SttEngine(QObject *parent) : QObject(parent) {
    qRegisterMetaType<QVector<float>>("QVector<float>");
    m_worker = new SttWorker;
    m_worker->moveToThread(&m_thread);

    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(this, &SttEngine::requestLoad, m_worker, &SttWorker::doLoad);
    connect(this, &SttEngine::requestUnload, m_worker, &SttWorker::doUnload);
    connect(this, &SttEngine::requestTranscribe, m_worker, &SttWorker::doTranscribe);

    connect(m_worker, &SttWorker::loaded, this, [this](const QString &dev) {
        m_ready = true;
        emit ready(dev);
    });
    connect(m_worker, &SttWorker::unloaded, this, [this]() { m_ready = false; });
    connect(m_worker, &SttWorker::failed, this,
            [this](const QString &msg) { emit error(msg); });
    connect(m_worker, &SttWorker::result, this,
            [this](const QString &text, const QString &lang, bool isFinal) {
                emit transcript(text, lang, isFinal);
            });

    m_thread.start();
}

SttEngine::~SttEngine() {
    m_thread.quit();
    m_thread.wait();
}

void SttEngine::load(const QString &backend, const QString &model,
                     const QString &device) {
    emit requestLoad(backend, model, device);
}

void SttEngine::unload() {
    m_ready = false;
    emit requestUnload();
}

void SttEngine::transcribe(const QVector<float> &pcm, int sampleRate,
                           const QString &language, bool isFinal) {
    emit requestTranscribe(pcm, sampleRate, language, isFinal);
}
