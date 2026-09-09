#include "Paster.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <unistd.h>

namespace {
QStringList shortcutKeySequence(const QString &shortcut) {
    if (shortcut == QLatin1String("ctrl+shift+v"))
        return {QStringLiteral("29:1"), QStringLiteral("42:1"), QStringLiteral("47:1"),
                QStringLiteral("47:0"), QStringLiteral("42:0"), QStringLiteral("29:0")};
    return {QStringLiteral("29:1"), QStringLiteral("47:1"),
            QStringLiteral("47:0"), QStringLiteral("29:0")};
}
}

Paster::Paster(QObject *parent) : Paster(Programs{}, parent) {}
Paster::Paster(const Programs &programs, QObject *parent) : QObject(parent), m_programs(programs) {
    if (m_programs.clipboardHelper.isEmpty())
        m_programs.clipboardHelper = QCoreApplication::applicationDirPath()
            + QStringLiteral("/scrybe-clipboard");
    m_deadline.setSingleShot(true);
    connect(&m_deadline, &QTimer::timeout, this, [this]() {
        if (!m_active) return;
        emit error(tr("Paste timed out. Your transcript is still available to retry."));
        if (m_active->phase == Phase::Finalizing) {
            m_active->helper->kill();
            stopInjector();
            m_active->cleanupPending = false;
            complete(false);
        } else {
            finalize(false);
        }
    });
}

Paster::~Paster() {
    // Do not let a child send delayed keys during application shutdown.
    for (auto *process : findChildren<QProcess *>()) {
        process->disconnect(this);
        if (process->property("clipboardOwner").toBool()) {
            process->closeWriteChannel();
            process->setParent(nullptr);
            connect(process, &QProcess::finished, process, &QObject::deleteLater);
            if (process->state() == QProcess::NotRunning) process->deleteLater();
        } else if (process->state() != QProcess::NotRunning) {
            process->kill();
        }
    }
}

quint64 Paster::paste(const QString &text) {
    const quint64 id = ++m_nextId;
    if (!text.isEmpty()) m_lastText = text;
    m_queue.enqueue({id, text});
    QTimer::singleShot(0, this, &Paster::startNext);
    return id;
}

bool Paster::active(quint64 id) const { return m_active && m_active->request.id == id; }

void Paster::startNext() {
    if (m_active || m_queue.isEmpty()) return;
    m_active = std::make_unique<Transaction>();
    m_active->request = m_queue.dequeue();
    const auto id = m_active->request.id;
    if (m_active->request.text.isEmpty()) { complete(false); return; }
    QSettings settings;
    m_active->restore = settings.value(QStringLiteral("paste/restoreClipboard"), true).toBool();
    m_active->restoreDelayMs = qBound(200, settings.value(QStringLiteral("paste/restoreDelayMs"), 1000).toInt(), 10000);
    m_active->shortcut = settings.value(QStringLiteral("paste/shortcut"), QStringLiteral("ctrl+v")).toString().toLower();
    auto *helper = new QProcess(this);
    helper->setProperty("clipboardOwner", true);
    m_active->helper = helper;
    connect(helper, &QProcess::started, this, [this, helper, id]() {
        if (!active(id)) return;
        const QJsonObject request{{QStringLiteral("text"), QString::fromLatin1(m_active->request.text.toUtf8().toBase64())},
                                  {QStringLiteral("restore"), m_active->restore}};
        helper->write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
        if (m_active->phase == Phase::Finalizing) helper->write("RESTORE\n");
    });
    auto buffer = std::make_shared<QByteArray>();
    connect(helper, &QProcess::readyReadStandardOutput, this, [this, helper, id, buffer]() {
        buffer->append(helper->readAllStandardOutput());
        if (buffer->size() > 8192) { helper->kill(); return; }
        while (buffer->contains('\n')) {
            const auto line = buffer->left(buffer->indexOf('\n'));
            buffer->remove(0, line.size() + 1);
            if (active(id)) helperLine(id, line);
        }
    });
    connect(helper, &QProcess::readyReadStandardError, helper, [helper]() { helper->readAllStandardError(); });
    connect(helper, &QProcess::errorOccurred, this, [this, helper, id](QProcess::ProcessError errorCode) {
        if (errorCode != QProcess::FailedToStart) return;
        if (active(id)) {
            emit error(tr("Paste failed: could not start the clipboard helper. Reinstall Scrybe."));
            complete(false);
        }
        helper->deleteLater();
    });
    connect(helper, &QProcess::finished, this, [this, helper, id](int, QProcess::ExitStatus) {
        if (active(id) && !m_active->completionRequested) {
            emit error(tr("The clipboard helper stopped before paste completed."));
            complete(false);
        }
        helper->deleteLater();
    });
    m_deadline.start(m_programs.prepareTimeoutMs);
    helper->start(m_programs.clipboardHelper, {});
}

void Paster::helperLine(quint64 id, const QByteArray &line) {
    if (line == "READY" && m_active->phase == Phase::Preparing) {
        m_deadline.stop();
        m_active->phase = Phase::Ready;
        // Process all currently queued ownership notifications before sending keys.
        QTimer::singleShot(0, this, [this, id]() { if (active(id)) inject(id); });
    } else if (line == "LOST") {
        const bool delivered = m_active->phase == Phase::Grace || m_active->phase == Phase::Finalizing;
        if (!delivered) emit error(tr("The clipboard changed before paste completed. Retry your transcript."));
        complete(delivered && m_active->success);
    } else if (line == "DONE" && m_active->phase == Phase::Finalizing) {
        complete(m_active->success);
    } else if (line.startsWith("ERROR ")) {
        emit error(tr("Paste failed: %1").arg(QString::fromUtf8(line.mid(6))));
        complete(false);
    }
}

void Paster::inject(quint64 id) {
    if (!active(id) || m_active->phase != Phase::Ready) return;
    m_active->phase = Phase::Injecting;
    auto *process = new QProcess(this);
    m_active->injector = process;
    auto env = QProcessEnvironment::systemEnvironment();
    const auto runtime = qEnvironmentVariable("XDG_RUNTIME_DIR", QStringLiteral("/run/user/%1").arg(getuid()));
    if (!env.contains(QStringLiteral("YDOTOOL_SOCKET")))
        env.insert(QStringLiteral("YDOTOOL_SOCKET"), runtime + QStringLiteral("/.ydotool_socket"));
    process->setProcessEnvironment(env);
    connect(process, &QProcess::errorOccurred, this, [this, process, id](QProcess::ProcessError code) {
        if (code != QProcess::FailedToStart) return;
        if (active(id) && m_active->phase == Phase::Injecting) {
            m_active->injector = nullptr;
            emit error(tr("Paste failed: could not run ydotool."));
            m_active->phase = Phase::Ready; // no key command was started
            finalize(false);
        }
        process->deleteLater();
    });
    connect(process, &QProcess::finished, this, [this, process, id](int code, QProcess::ExitStatus status) {
        process->deleteLater();
        if (!active(id) || m_active->phase != Phase::Injecting) return;
        m_active->injector = nullptr;
        if (status != QProcess::NormalExit || code != 0) {
            emit error(tr("Paste failed (ydotool). Check that ydotoold is running and the input group applies."));
            finalize(false);
            return;
        }
        m_deadline.stop();
        m_active->success = true;
        m_active->phase = Phase::Grace;
        QTimer::singleShot(m_active->restoreDelayMs, this, [this, id]() {
            if (active(id) && m_active->phase == Phase::Grace) finalize(true);
        });
    });
    m_deadline.start(m_programs.commandTimeoutMs);
    process->start(m_programs.keyInjector, QStringList{QStringLiteral("key")} + shortcutKeySequence(m_active->shortcut));
}

void Paster::stopInjector() {
    if (m_active && m_active->injector) {
        auto *process = m_active->injector;
        m_active->injector = nullptr;
        process->kill();
    }
}

// A killed injector may have sent only key-down events. Releasing those keys
// is best effort, runs after that process has stopped, and holds the queue until
// it completes (or the finalization deadline expires).
void Paster::releaseKeys() {
    const auto id = m_active->request.id;
    m_active->cleanupPending = true;
    auto *previous = m_active->injector;
    m_active->injector = nullptr;
    auto launched = std::make_shared<bool>(false);
    auto launch = [this, id, launched]() {
        if (*launched || !active(id) || !m_active->cleanupPending) return;
        *launched = true;
        auto *release = new QProcess(this);
        m_active->injector = release;
        auto env = QProcessEnvironment::systemEnvironment();
        if (!env.contains(QStringLiteral("YDOTOOL_SOCKET")))
            env.insert(QStringLiteral("YDOTOOL_SOCKET"), qEnvironmentVariable("XDG_RUNTIME_DIR",
                QStringLiteral("/run/user/%1").arg(getuid())) + QStringLiteral("/.ydotool_socket"));
        release->setProcessEnvironment(env);
        auto done = [this, id, release](bool success) {
            release->deleteLater();
            if (!active(id) || !m_active->cleanupPending) return;
            m_active->injector = nullptr;
            m_active->cleanupPending = false;
            if (!success) emit error(tr("Could not release paste shortcut keys. Press and release Ctrl and Shift."));
            if (m_active->completionRequested) complete(m_active->completionSuccess && success);
        };
        connect(release, &QProcess::finished, this, [done](int code, QProcess::ExitStatus status) {
            done(code == 0 && status == QProcess::NormalExit);
        });
        connect(release, &QProcess::errorOccurred, this, [done](QProcess::ProcessError code) {
            if (code == QProcess::FailedToStart) done(false);
        });
        release->start(m_programs.keyInjector, {QStringLiteral("key"), QStringLiteral("47:0"),
            QStringLiteral("42:0"), QStringLiteral("29:0")});
    };
    if (!previous || previous->state() == QProcess::NotRunning) {
        QTimer::singleShot(0, this, launch);
    } else {
        connect(previous, &QProcess::finished, this, [launch](int, QProcess::ExitStatus) { launch(); });
        connect(previous, &QProcess::errorOccurred, this, [launch](QProcess::ProcessError code) {
            if (code == QProcess::FailedToStart) launch();
        });
        previous->kill();
    }
}

void Paster::finalize(bool success) {
    if (!m_active) return;
    const bool needsRelease = m_active->phase == Phase::Injecting;
    m_active->success = success;
    m_active->phase = Phase::Finalizing;
    if (needsRelease) releaseKeys();
    m_deadline.start(m_programs.commandTimeoutMs);
    if (m_active->helper->state() == QProcess::Running)
        m_active->helper->write(m_active->restore ? "RESTORE\n" : "KEEP\n");
}

void Paster::complete(bool success) {
    if (!m_active) return;
    if (m_active->phase == Phase::Injecting || m_active->cleanupPending) {
        m_active->completionRequested = true;
        m_active->completionSuccess = success;
        if (m_active->phase == Phase::Injecting) {
            m_active->phase = Phase::Finalizing;
            m_deadline.start(m_programs.commandTimeoutMs);
            releaseKeys();
        }
        return;
    }
    m_deadline.stop();
    stopInjector();
    const auto id = m_active->request.id;
    m_active.reset();
    emit finished(id, success);
    QTimer::singleShot(0, this, &Paster::startNext);
}

void Paster::cancel() {
    const auto queued = m_queue;
    m_queue.clear();
    if (m_active) finalize(false);
    for (const auto &request : queued) emit finished(request.id, false);
}
