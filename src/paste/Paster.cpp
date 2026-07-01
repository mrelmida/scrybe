#include "Paster.h"

#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QTimer>

namespace {
QString ydotoolSocket() {
    const QString rt = qEnvironmentVariable("XDG_RUNTIME_DIR",
                                            QStringLiteral("/run/user/1000"));
    return rt + QStringLiteral("/.ydotool_socket");
}
} // namespace

Paster::Paster(QObject *parent) : QObject(parent) {
    m_restoreClipboard =
        QSettings().value(QStringLiteral("paste/restoreClipboard"), true).toBool();
}

void Paster::paste(const QString &text) {
    if (text.isEmpty())
        return;

    // 1. Save the current clipboard (text only; ignore non-text content).
    m_savedClipboard.clear();
    if (m_restoreClipboard) {
        QProcess wp;
        wp.start(QStringLiteral("wl-paste"), {QStringLiteral("--no-newline")});
        if (wp.waitForFinished(500) && wp.exitCode() == 0)
            m_savedClipboard = QString::fromUtf8(wp.readAllStandardOutput());
    }

    // 2. Put our text on the clipboard.
    setClipboard(text);

    // 3. Once the clipboard is set and focus has returned to the target app,
    //    inject Ctrl+V, then restore the previous clipboard shortly after.
    QTimer::singleShot(100, this, [this]() {
        sendCtrlV();
        if (m_restoreClipboard && !m_savedClipboard.isEmpty()) {
            const QString prev = m_savedClipboard;
            QTimer::singleShot(400, this, [this, prev]() { setClipboard(prev); });
        }
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

void Paster::sendCtrlV() {
    QProcess p;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("YDOTOOL_SOCKET"), ydotoolSocket());
    p.setProcessEnvironment(env);
    // Linux input keycodes: 29 = LEFTCTRL, 47 = V. Press ctrl, press v, release.
    p.start(QStringLiteral("ydotool"),
            {QStringLiteral("key"), QStringLiteral("29:1"), QStringLiteral("47:1"),
             QStringLiteral("47:0"), QStringLiteral("29:0")});
    if (!p.waitForFinished(1500) || p.exitCode() != 0) {
        emit error(QStringLiteral(
            "Paste failed (ydotool). If this persists, log out/in once so the "
            "'input' group applies."));
    }
}
