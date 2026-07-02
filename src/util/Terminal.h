#pragma once

#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

// Run a shell command in a detached terminal emulator so the user can watch
// progress and answer sudo prompts. Used for on-demand backend installs and
// in-place updates. Returns false if no terminal emulator is available.
namespace scrybe {

inline QStringList terminalArgv(const QString &shellCmd) {
    struct Term { const char *bin; QStringList argv; };
    const QString wrapped =
        shellCmd + QStringLiteral("; echo; read -n1 -r -p 'Press any key to close…'");
    const Term terms[] = {
        {"konsole",       {QStringLiteral("-e"), QStringLiteral("bash"), QStringLiteral("-lc"), wrapped}},
        {"gnome-terminal",{QStringLiteral("--"), QStringLiteral("bash"), QStringLiteral("-lc"), wrapped}},
        {"kitty",         {QStringLiteral("bash"), QStringLiteral("-lc"), wrapped}},
        {"alacritty",     {QStringLiteral("-e"), QStringLiteral("bash"), QStringLiteral("-lc"), wrapped}},
        {"xterm",         {QStringLiteral("-e"), QStringLiteral("bash"), QStringLiteral("-lc"), wrapped}},
    };
    for (const Term &t : terms) {
        const QString path = QStandardPaths::findExecutable(QString::fromLatin1(t.bin));
        if (!path.isEmpty())
            return QStringList{path} << t.argv;
    }
    return {};
}

inline bool launchTerminal(const QString &shellCmd) {
    const QStringList cmd = terminalArgv(shellCmd);
    if (cmd.isEmpty())
        return false;
    return QProcess::startDetached(cmd.first(), cmd.mid(1));
}

} // namespace scrybe
