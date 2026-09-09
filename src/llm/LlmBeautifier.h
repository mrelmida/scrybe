#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

// Cleans up transcribed text with a local Ollama model (fix punctuation/casing,
// drop filler words) without changing meaning. Falls back to the raw text on any
// error, so dictation still works if Ollama is down.
class LlmBeautifier : public QObject {
    Q_OBJECT
public:
    explicit LlmBeautifier(QObject *parent = nullptr);

    // style: "format" (clean up) | "markdown" (structure) | "summary" (condense)
    void beautify(const QString &text, const QString &style);

    // Settings-UI helpers, separate signals so they can't be mistaken for a
    // dictation result by the Controller state machine:
    // Run `text` through an (unsaved) preset prompt exactly as beautify would.
    void preview(const QString &text, const QString &systemPrompt, double temp);
    // Ask the model to write a preset system prompt from a plain description.
    void draftPreset(const QString &description);

signals:
    void done(const QString &text);        // formatted text
    void failed(const QString &message);   // caller should fall back to raw
    void previewDone(const QString &text);
    void previewFailed(const QString &message);
    void draftDone(const QString &text);
    void draftFailed(const QString &message);

private:
    // POST /api/generate with the configured endpoint/model; replies are
    // handled by the caller via the returned reply's finished signal.
    QNetworkReply *post(const QString &system, const QString &prompt, double temp);

    QNetworkAccessManager *m_nam = nullptr;
};
