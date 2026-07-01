#pragma once

#include <QObject>
#include <QString>

// Pastes text into the currently-focused application via the clipboard, to
// avoid emitting the text as individual keystrokes:
//   1. save the current clipboard,
//   2. put the text on the clipboard (wl-copy),
//   3. inject Ctrl+V (ydotool),
//   4. restore the previous clipboard.
class Paster : public QObject {
    Q_OBJECT
public:
    explicit Paster(QObject *parent = nullptr);

    void paste(const QString &text);

signals:
    void error(const QString &message);

private:
    void setClipboard(const QString &text);
    void sendCtrlV();

    QString m_savedClipboard;
    bool m_restoreClipboard = true;
};
