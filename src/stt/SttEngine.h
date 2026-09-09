#pragma once

#include <QObject>
#include <QString>
#include <QThread>
#include <QVector>

#include <atomic>
#include <memory>
#include <functional>

class ISttBackend;
using SttBackendFactory = std::function<std::unique_ptr<ISttBackend>(const QString &)>;

// Worker that owns the active backend and runs all STT work on its own thread
// (model load + transcribe are blocking and can take seconds).
class SttWorker : public QObject {
    Q_OBJECT
public:
    explicit SttWorker(SttBackendFactory factory, QObject *parent = nullptr);
    ~SttWorker() override;

    // Thread-safe: when set, queued *partial* requests are skipped so a stale
    // preview job can't delay the final transcription behind it.
    void setDropPartials(bool drop) { m_dropPartials.store(drop); }
    void setSession(quint64 session) { m_session.store(session); }
    void setModelGeneration(quint64 generation) { m_modelGeneration.store(generation); }

public slots:
    void doLoad(const QString &backend, const QString &model, const QString &device, quint64 generation);
    void doUnload(quint64 generation);
    void doTranscribe(const QVector<float> &pcm, int sampleRate,
                      const QString &language, bool isFinal, quint64 session, quint64 request);

signals:
    void loaded(const QString &device, quint64 generation);
    void unloaded(quint64 generation);
    void failed(const QString &message, quint64 generation);
    void result(const QString &text, const QString &language, bool isFinal,
                quint64 session, quint64 request);
    void transcriptionFailed(const QString &message, bool isFinal,
                             quint64 session, quint64 request);

private:
    SttBackendFactory m_factory;
    std::unique_ptr<ISttBackend> m_backend;
    std::atomic<quint64> m_session{0};
    std::atomic<quint64> m_modelGeneration{0};
    QString m_backendType;
    std::atomic<bool> m_dropPartials{false};
};

// GUI-thread facade. Public methods marshal to the worker via queued signals;
// results come back as queued signals on the GUI thread.
class SttEngine : public QObject {
    Q_OBJECT
public:
    explicit SttEngine(QObject *parent = nullptr, SttBackendFactory factory = {});
    ~SttEngine() override;

    bool isReady() const { return m_ready; }

    void load(const QString &backend, const QString &model, const QString &device, quint64 generation = 0);
    void unload(quint64 generation = 0);
    void setModelGeneration(quint64 generation);
    quint64 transcribe(const QVector<float> &pcm, int sampleRate,
                       const QString &language, bool isFinal, quint64 session = 0);

    // Invalidates queued and in-flight results immediately without blocking.
    void setSession(quint64 session);

    // Skip queued previews within the current session.
    void setDropPartials(bool drop);

signals:
    void ready(const QString &device, quint64 generation);
    void error(const QString &message, quint64 generation);
    void transcript(const QString &text, const QString &language, bool isFinal,
                    quint64 session, quint64 request);
    void transcriptionFailed(const QString &message, bool isFinal,
                             quint64 session, quint64 request);

    // Internal → worker (queued).
    void requestLoad(const QString &backend, const QString &model, const QString &device, quint64 generation);
    void requestUnload(quint64 generation);
    void requestTranscribe(const QVector<float> &pcm, int sampleRate,
                           const QString &language, bool isFinal, quint64 session, quint64 request);

private:
    QThread m_thread;
    SttWorker *m_worker = nullptr;
    bool m_ready = false;
    quint64 m_session = 0;
    quint64 m_modelGeneration = 0;
    quint64 m_nextRequest = 0;
};
