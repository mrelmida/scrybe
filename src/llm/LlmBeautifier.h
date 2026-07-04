#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;

// Cleans up transcribed text with a local Ollama model (fix punctuation/casing,
// drop filler words) without changing meaning. Falls back to the raw text on any
// error, so dictation still works if Ollama is down.
class LlmBeautifier : public QObject {
    Q_OBJECT
public:
    explicit LlmBeautifier(QObject *parent = nullptr);

    // style: "format" (clean up) | "markdown" (structure) | "summary" (condense)
    void beautify(const QString &text, const QString &style);

signals:
    void done(const QString &text);        // formatted text
    void failed(const QString &message);   // caller should fall back to raw

private:
    QNetworkAccessManager *m_nam = nullptr;
};
