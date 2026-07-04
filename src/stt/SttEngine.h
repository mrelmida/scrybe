#pragma once

#include <QObject>
#include <QString>
#include <QThread>
#include <QVector>

#include <atomic>
#include <memory>

class ISttBackend;

// Worker that owns the active backend and runs all STT work on its own thread
// (model load + transcribe are blocking and can take seconds).
class SttWorker : public QObject {
    Q_OBJECT
public:
    explicit SttWorker(QObject *parent = nullptr);
    ~SttWorker() override;

    // Thread-safe: when set, queued *partial* requests are skipped so a stale
    // preview job can't delay the final transcription behind it.
    void setDropPartials(bool drop) { m_dropPartials.store(drop); }

public slots:
    void doLoad(const QString &backend, const QString &model, const QString &device);
    void doUnload();
    void doTranscribe(const QVector<float> &pcm, int sampleRate,
                      const QString &language, bool isFinal);

signals:
    void loaded(const QString &device);
    void unloaded();
    void failed(const QString &message);
    void result(const QString &text, const QString &language, bool isFinal);

private:
    std::unique_ptr<ISttBackend> m_backend;
    QString m_backendType;
    std::atomic<bool> m_dropPartials{false};
};

// GUI-thread facade. Public methods marshal to the worker via queued signals;
// results come back as queued signals on the GUI thread.
class SttEngine : public QObject {
    Q_OBJECT
public:
    explicit SttEngine(QObject *parent = nullptr);
    ~SttEngine() override;

    bool isReady() const { return m_ready; }

    void load(const QString &backend, const QString &model, const QString &device);
    void unload();
    void transcribe(const QVector<float> &pcm, int sampleRate,
                    const QString &language, bool isFinal);

    // Skip queued partial (preview) requests; finals are never dropped.
    void setDropPartials(bool drop);

signals:
    void ready(const QString &device);
    void error(const QString &message);
    void transcript(const QString &text, const QString &language, bool isFinal);

    // Internal → worker (queued).
    void requestLoad(const QString &backend, const QString &model, const QString &device);
    void requestUnload();
    void requestTranscribe(const QVector<float> &pcm, int sampleRate,
                           const QString &language, bool isFinal);

private:
    QThread m_thread;
    SttWorker *m_worker = nullptr;
    bool m_ready = false;
};
