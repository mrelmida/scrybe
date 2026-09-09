#pragma once

#include <QObject>
#include <QQueue>
#include <QString>
#include <QTimer>
#include <memory>

class QProcess;

// Serialized asynchronous clipboard transactions. The clipboard helper owns the
// Wayland selection and preserves every MIME representation until restoration.
// finished() is emitted exactly once per ID, including cancellation/failure.
class Paster : public QObject {
    Q_OBJECT
public:
    struct Programs {
        QString clipboardHelper;
        QString keyInjector = QStringLiteral("ydotool");
        int prepareTimeoutMs = 8000;
        int commandTimeoutMs = 2500;
    };
    explicit Paster(QObject *parent = nullptr);
    explicit Paster(const Programs &programs, QObject *parent = nullptr);
    ~Paster() override;

    quint64 paste(const QString &text);
    void cancel();
    QString lastText() const { return m_lastText; }

signals:
    void error(const QString &message);
    void finished(quint64 transactionId, bool success);

private:
    struct Request { quint64 id; QString text; };
    enum class Phase { Preparing, Ready, Injecting, Grace, Finalizing };
    struct Transaction {
        Request request;
        QProcess *helper = nullptr;
        QProcess *injector = nullptr;
        Phase phase = Phase::Preparing;
        bool restore = true;
        bool success = false;
        bool cleanupPending = false;
        bool completionRequested = false;
        bool completionSuccess = false;
        int restoreDelayMs = 1000;
        QString shortcut;
    };
    void startNext();
    void inject(quint64 id);
    void helperLine(quint64 id, const QByteArray &line);
    void finalize(bool success);
    void complete(bool success);
    bool active(quint64 id) const;
    void stopInjector();
    void releaseKeys();

    Programs m_programs;
    QQueue<Request> m_queue;
    std::unique_ptr<Transaction> m_active;
    QTimer m_deadline;
    quint64 m_nextId = 0;
    QString m_lastText;
};
