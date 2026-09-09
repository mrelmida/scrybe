#include "SttEngine.h"

#include "Resample.h"
#include "SttBackend.h"

#include <QtGlobal>

#include <vector>

// ---------------------------------------------------------------------------- //
// SttWorker (runs on the worker thread)
// ---------------------------------------------------------------------------- //
SttWorker::SttWorker(SttBackendFactory factory, QObject *parent)
    : QObject(parent), m_factory(std::move(factory)) {}
SttWorker::~SttWorker() = default;

void SttWorker::doLoad(const QString &backend, const QString &model,
                       const QString &device) {
    if (!m_backend || m_backendType != backend) {
        m_backend = m_factory(backend);
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
                             const QString &language, bool isFinal,
                             quint64 session, quint64 request) {
    if (session != m_session.load() || (!isFinal && m_dropPartials.load()))
        return;
    if (!m_backend) {
        emit transcriptionFailed(QStringLiteral("STT backend is not loaded yet."),
                                 isFinal, session, request);
        return;
    }
    std::vector<float> audio = scrybe::resampleTo16k(pcm, sampleRate);
    if (audio.empty()) {
        emit result(QString(), QString(), isFinal, session, request);
        return;
    }
    QString text, err;
    const bool ok = m_backend->transcribe(audio, language, &text, &err);
    if (session != m_session.load())
        return;
    if (ok)
        emit result(text, QString(), isFinal, session, request);
    else
        emit transcriptionFailed(err, isFinal, session, request);
}

// ---------------------------------------------------------------------------- //
// SttEngine (GUI-thread facade)
// ---------------------------------------------------------------------------- //
SttEngine::SttEngine(QObject *parent, SttBackendFactory factory) : QObject(parent) {
    qRegisterMetaType<QVector<float>>("QVector<float>");
    m_worker = new SttWorker(factory ? std::move(factory) : makeSttBackend);
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
            [this](const QString &text, const QString &lang, bool isFinal,
                   quint64 session, quint64 request) {
                if (session == m_session)
                    emit transcript(text, lang, isFinal, session, request);
            });
    connect(m_worker, &SttWorker::transcriptionFailed, this,
            [this](const QString &message, bool isFinal, quint64 session, quint64 request) {
                if (session == m_session)
                    emit transcriptionFailed(message, isFinal, session, request);
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

quint64 SttEngine::transcribe(const QVector<float> &pcm, int sampleRate,
                              const QString &language, bool isFinal, quint64 session) {
    const quint64 request = ++m_nextRequest;
    emit requestTranscribe(pcm, sampleRate, language, isFinal, session, request);
    return request;
}

void SttEngine::setSession(quint64 session) {
    m_session = session;
    m_worker->setSession(session);
}

void SttEngine::setDropPartials(bool drop) {
    m_worker->setDropPartials(drop);   // atomic; safe to call cross-thread
}
