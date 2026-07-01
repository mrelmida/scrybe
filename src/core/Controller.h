#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <functional>

class AudioCapture;
class SttEngine;
class Paster;
class LlmBeautifier;
class QTimer;

// Central state machine driving the QML overlay.
//
// M3: audio from the real microphone (AudioCapture) is transcribed on-device by
// OpenVINO Whisper (SttEngine). LLM beautify + clipboard paste land in M4.
class Controller : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString stateName READ stateName NOTIFY stateChanged)
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    Q_PROPERTY(qreal level READ level NOTIFY levelChanged)
    Q_PROPERTY(QString transcript READ transcript NOTIFY transcriptChanged)
    Q_PROPERTY(bool llmEnabled READ llmEnabled WRITE setLlmEnabled NOTIFY llmEnabledChanged)
    Q_PROPERTY(bool modelReady READ modelReady NOTIFY modelReadyChanged)
    Q_PROPERTY(bool previewEnabled READ previewEnabled WRITE setPreviewEnabled NOTIFY previewEnabledChanged)
    Q_PROPERTY(QString beautifyStyle READ beautifyStyle WRITE setBeautifyStyle NOTIFY beautifyStyleChanged)
    Q_PROPERTY(QString backend READ backend WRITE setBackend NOTIFY backendChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(bool settingsOpen READ settingsOpen WRITE setSettingsOpen NOTIFY settingsOpenChanged)
    Q_PROPERTY(QString model READ model WRITE setModel NOTIFY modelChanged)

public:
    enum State { Idle, Listening, Transcribing, Beautifying, Pasting };
    Q_ENUM(State)

    explicit Controller(QObject *parent = nullptr);

    State state() const { return m_state; }
    QString stateName() const;
    bool active() const { return m_state != Idle; }
    qreal level() const { return m_level; }
    QString transcript() const { return m_transcript; }
    bool llmEnabled() const { return m_llmEnabled; }
    void setLlmEnabled(bool on);
    bool modelReady() const { return m_modelReady; }
    bool previewEnabled() const { return m_previewEnabled; }
    void setPreviewEnabled(bool on);

    QString model() const { return m_model; }
    QString beautifyStyle() const { return m_beautifyStyle; }
    void setBeautifyStyle(const QString &s);
    QString backend() const;
    void setBackend(const QString &b);
    QString language() const;
    void setLanguage(const QString &l);
    bool settingsOpen() const { return m_settingsOpen; }
    void setSettingsOpen(bool on);

    // Exposed to the settings UI.
    Q_INVOKABLE QVariantList modelList() const;   // [{key,label}]
    Q_INVOKABLE QVariantList styleList() const;    // builtins + custom presets
    Q_INVOKABLE QStringList backendList() const;
    Q_INVOKABLE QStringList presetNames() const;
    Q_INVOKABLE QString presetPrompt(const QString &name) const;
    Q_INVOKABLE void savePreset(const QString &name, const QString &prompt);
    Q_INVOKABLE void deletePreset(const QString &name);

public slots:
    void toggle();
    void startListening();
    void stopListening();   // finalize + paste — "send"
    void send();            // Enter: finalize immediately
    void cancel();          // Esc: discard, no paste
    void setModel(const QString &key);   // switch STT model (downloads if needed)

signals:
    void stateChanged();
    void levelChanged();
    void transcriptChanged();
    void llmEnabledChanged();
    void modelReadyChanged();
    void previewEnabledChanged();
    void beautifyStyleChanged();
    void backendChanged();
    void languageChanged();
    void settingsOpenChanged();
    void presetsChanged();
    void modelChanged();
    void requestShow();
    void requestHide();
    void notify(const QString &message);   // non-fatal user-facing messages

private:
    void setState(State s);
    void setLevel(qreal v);
    void setTranscript(const QString &t);
    void onTranscript(const QString &text, bool isFinal);
    void finish();                            // brief Pasting state → Idle
    void requestPartial();                    // rolling live transcription
    QString device() const;
    QString activeBackend() const;            // resolves "auto" to a real backend
    QString modelDirFor(const QString &key) const;
    void ensureModelLoaded();                 // load current model if not resident
    void ensureDownloaded(const QString &key, std::function<void(bool)> cb);
    void scheduleUnload();                    // unload after idle grace period

    State m_state = Idle;
    qreal m_level = 0.0;
    QString m_transcript;
    bool m_llmEnabled = false;

    AudioCapture *m_audio = nullptr;
    SttEngine *m_stt = nullptr;
    Paster *m_paster = nullptr;
    LlmBeautifier *m_llm = nullptr;
    QString m_language = QStringLiteral("auto");
    QString m_model = QStringLiteral("small");
    QString m_beautifyStyle = QStringLiteral("format");
    bool m_settingsOpen = false;

    QTimer *m_partialTimer = nullptr;
    QTimer *m_unloadTimer = nullptr;   // unloads the model after idle
    bool m_sttBusy = false;   // a transcription is in flight
    bool m_cancelled = false; // ignore results after a cancel
    bool m_modelReady = false;
    bool m_modelLoading = false;
    bool m_previewEnabled = true;
};
