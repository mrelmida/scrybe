#pragma once

#include <QObject>
#include <QString>

// Pastes text into the currently-focused application via the clipboard, to
// avoid emitting the text as individual keystrokes:
//   1. save the current clipboard (wl-paste, async),
//   2. put the text on the clipboard (wl-copy),
//   3. inject the paste shortcut (ydotool),
//   4. restore the previous clipboard after a grace period.
//
// Settings (read per paste, so edits apply immediately):
//   paste/restoreClipboard  restore the previous clipboard (default true)
//   paste/restoreDelayMs    grace period before restoring (default 1000)
//   paste/shortcut          "ctrl+v" (default) or "ctrl+shift+v" (terminals)
class Paster : public QObject {
    Q_OBJECT
public:
    explicit Paster(QObject *parent = nullptr);

    void paste(const QString &text);

signals:
    void error(const QString &message);

private:
    void setClipboard(const QString &text);
    void injectAndRestore(bool restore, int restoreDelayMs);
    void sendPasteShortcut();

    QString m_savedClipboard;
    bool m_hadSavedClipboard = false;   // wl-paste succeeded (even if empty)
};
