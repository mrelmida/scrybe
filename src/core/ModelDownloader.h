#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

class QProcess;

// One cancellable OpenVINO download. Completion is emitted exactly once even
// when process startup, timeout and process-exit signals overlap.
class ModelDownloader : public QObject {
    Q_OBJECT
public:
    explicit ModelDownloader(QObject *parent = nullptr, const QString &program = {},
                             int timeoutMs = 30 * 60 * 1000);
    virtual void ensure(const QString &directory, const QString &repository, quint64 generation);
    virtual void cancel();

signals:
    void finished(bool success, const QString &message, quint64 generation);

private:
    void complete(QProcess *process, bool success, const QString &message,
                  quint64 generation);
    QPointer<QProcess> m_process;
    QString m_program;
    int m_timeoutMs;
    quint64 m_generation = 0;
};
