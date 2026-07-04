#include "Paster.h"

#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStringList>
#include <QTimer>

#include <unistd.h>

namespace {
QString ydotoolSocket() {
    const QString rt = qEnvironmentVariable(
        "XDG_RUNTIME_DIR", QStringLiteral("/run/user/%1").arg(getuid()));
    return rt + QStringLiteral("/.ydotool_socket");
}

// Linux input keycodes: 29 = LEFTCTRL, 42 = LEFTSHIFT, 47 = V.
QStringList shortcutKeySequence(const QString &shortcut) {
    if (shortcut == QLatin1String("ctrl+shift+v"))
        return {QStringLiteral("29:1"), QStringLiteral("42:1"), QStringLiteral("47:1"),
                QStringLiteral("47:0"), QStringLiteral("42:0"), QStringLiteral("29:0")};
    return {QStringLiteral("29:1"), QStringLiteral("47:1"),
            QStringLiteral("47:0"), QStringLiteral("29:0")};
}
} // namespace

Paster::Paster(QObject *parent) : QObject(parent) {}

void Paster::paste(const QString &text) {
    if (text.isEmpty())
        return;

    QSettings s;
    const bool restore =
        s.value(QStringLiteral("paste/restoreClipboard"), true).toBool();
    const int restoreDelayMs =
        s.value(QStringLiteral("paste/restoreDelayMs"), 1000).toInt();

    m_savedClipboard.clear();
    m_hadSavedClipboard = false;

    if (!restore) {
        setClipboard(text);
        injectAndRestore(false, 0);
        return;
    }

    // 1. Save the current clipboard asynchronously (text only), then continue.
    auto *wp = new QProcess(this);
    connect(wp, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, wp, text, restoreDelayMs](int code, QProcess::ExitStatus) {
                if (code == 0) {
                    m_savedClipboard = QString::fromUtf8(wp->readAllStandardOutput());
                    m_hadSavedClipboard = true;
                }
                wp->deleteLater();
                setClipboard(text);
                injectAndRestore(true, restoreDelayMs);
            });
    // FailedToStart is the only error that never reaches finished(); any other
    // failure (crash/kill) falls through to the finished handler above.
    connect(wp, &QProcess::errorOccurred, this,
            [this, wp, text, restoreDelayMs](QProcess::ProcessError e) {
                if (e != QProcess::FailedToStart)
                    return;
                wp->deleteLater();
                setClipboard(text);
                injectAndRestore(true, restoreDelayMs);
            });
    // Don't hang if something holds the selection open.
    QTimer::singleShot(500, wp, [wp]() {
        if (wp->state() != QProcess::NotRunning)
            wp->kill();
    });
    wp->start(QStringLiteral("wl-paste"), {QStringLiteral("--no-newline")});
}

void Paster::injectAndRestore(bool restore, int restoreDelayMs) {
    // 2. Once the clipboard is set and focus has returned to the target app,
    //    inject the paste shortcut, then restore the previous clipboard after a
    //    grace period (long enough that slow apps have read our text).
    QTimer::singleShot(100, this, [this, restore, restoreDelayMs]() {
        sendPasteShortcut();
        if (!restore || !m_hadSavedClipboard)
            return;
        const QString prev = m_savedClipboard;
        QTimer::singleShot(qMax(200, restoreDelayMs), this, [this, prev]() {
            if (prev.isEmpty()) {
                // The clipboard was empty before — put it back that way.
                auto *p = new QProcess(this);
                connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                        p, &QObject::deleteLater);
                p->start(QStringLiteral("wl-copy"), {QStringLiteral("--clear")});
            } else {
                setClipboard(prev);
            }
        });
    });
}

void Paster::setClipboard(const QString &text) {
    // wl-copy reads the value from stdin, then forks to serve the selection.
    auto *p = new QProcess(this);
    connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            p, &QObject::deleteLater);
    p->start(QStringLiteral("wl-copy"), {QStringLiteral("--type"),
                                         QStringLiteral("text/plain;charset=utf-8")});
    if (p->waitForStarted(500)) {
        p->write(text.toUtf8());
        p->closeWriteChannel();
    }
}

void Paster::sendPasteShortcut() {
    const QString shortcut = QSettings()
        .value(QStringLiteral("paste/shortcut"), QStringLiteral("ctrl+v"))
        .toString().toLower();

    auto *p = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("YDOTOOL_SOCKET"), ydotoolSocket());
    p->setProcessEnvironment(env);
    connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, p](int code, QProcess::ExitStatus st) {
                p->deleteLater();
                if (st != QProcess::NormalExit || code != 0)
                    emit error(tr(
                        "Paste failed (ydotool). If this persists, log out/in "
                        "once so the 'input' group applies."));
            });
    connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;   // other errors still reach the finished handler
        p->deleteLater();
        emit error(tr("Paste failed: could not run ydotool."));
    });
    p->start(QStringLiteral("ydotool"),
             QStringList{QStringLiteral("key")} + shortcutKeySequence(shortcut));
}
