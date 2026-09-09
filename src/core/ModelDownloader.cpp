#include "ModelDownloader.h"
#include "util/PythonEnv.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTimer>

namespace {
QString markerPath(const QString &directory) {
    return QDir(directory).filePath(QStringLiteral(".complete"));
}
bool hasModelFiles(const QString &directory) {
    const QDir dir(directory);
    return !dir.entryList({QStringLiteral("*.xml")}, QDir::Files).isEmpty() &&
           !dir.entryList({QStringLiteral("*.bin")}, QDir::Files).isEmpty();
}
}

ModelDownloader::ModelDownloader(QObject *parent, const QString &program, int timeoutMs)
    : QObject(parent), m_program(program), m_timeoutMs(timeoutMs) {}

void ModelDownloader::ensure(const QString &directory, const QString &repository,
                             quint64 generation) {
    cancel();
    if (QFileInfo::exists(markerPath(directory))) {
        emit finished(true, {}, generation);
        return;
    }
    auto *process = new QProcess(this);
    m_process = process;
    m_generation = generation;
    process->setProgram(m_program.isEmpty() ? scrybe::pythonExecutable() : m_program);
    process->setArguments({QStringLiteral("-c"),
        QStringLiteral("import sys\n"
                       "from huggingface_hub import snapshot_download\n"
                       "snapshot_download(repo_id=sys.argv[1], local_dir=sys.argv[2],"
                       "allow_patterns=['*.xml','*.bin','*.json','*.txt'])"),
        repository, directory});
    connect(process, &QProcess::errorOccurred, this,
            [this, process, generation](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            complete(process, false, tr("Could not start the model downloader: %1. Check the Python environment.")
                     .arg(process->errorString()), generation);
    });
    connect(process, &QProcess::finished, this,
            [this, process, directory, generation](int code, QProcess::ExitStatus status) {
        if (m_process != process) return;
        bool ok = status == QProcess::NormalExit && code == 0 && hasModelFiles(directory);
        QString message;
        if (ok) {
            QFile marker(markerPath(directory));
            ok = marker.open(QIODevice::WriteOnly) && marker.write("downloaded by scrybe\n") > 0;
            if (!ok) message = tr("The downloaded model could not be marked complete. Check free space and directory permissions.");
        } else {
            message = tr("Model download failed. Check your connection and free disk space, then try recording again.");
        }
        complete(process, ok, message, generation);
    });
    auto *timer = new QTimer(process);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, process, generation]() {
        complete(process, false, tr("Model download timed out. Check your connection and try recording again."), generation);
    });
    timer->start(m_timeoutMs);
    process->start();
}

void ModelDownloader::complete(QProcess *process, bool success, const QString &message,
                               quint64 generation) {
    if (m_process != process) return;
    m_process.clear();
    if (auto *timer = process->findChild<QTimer *>()) timer->stop();
    if (process->state() != QProcess::NotRunning) {
        // Keep the process alive until it exits; its destructor must not wait
        // for a running child on the GUI thread.
        connect(process, &QProcess::finished, process, &QObject::deleteLater);
        process->kill();
    } else {
        process->deleteLater();
    }
    emit finished(success, message, generation);
}

void ModelDownloader::cancel() {
    if (m_process)
        complete(m_process, false, {}, m_generation);
}
