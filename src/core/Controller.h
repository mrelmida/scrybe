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
class Updater;
class QNetworkAccessManager;
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
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(QString micDevice READ micDevice WRITE setMicDevice NOTIFY micDeviceChanged)
    Q_PROPERTY(qreal micGain READ micGain WRITE setMicGain NOTIFY micGainChanged)
    Q_PROPERTY(bool vadEnabled READ vadEnabled WRITE setVadEnabled NOTIFY vadEnabledChanged)
    Q_PROPERTY(qreal autoSendSecs READ autoSendSecs WRITE setAutoSendSecs NOTIFY autoSendSecsChanged)
    Q_PROPERTY(QString llmModel READ llmModel WRITE setLlmModel NOTIFY llmModelChanged)
    Q_PROPERTY(QString llmEndpoint READ llmEndpoint WRITE setLlmEndpoint NOTIFY llmEndpointChanged)
    Q_PROPERTY(QObject *updater READ updater CONSTANT)

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
    QString theme() const { return m_theme; }
    void setTheme(const QString &t);
    QString beautifyStyle() const { return m_beautifyStyle; }
    void setBeautifyStyle(const QString &s);
    QString backend() const;
    void setBackend(const QString &b);
    QString language() const;
    void setLanguage(const QString &l);
    bool settingsOpen() const { return m_settingsOpen; }
    void setSettingsOpen(bool on);

    QString micDevice() const;
    void setMicDevice(const QString &id);
    qreal micGain() const;
    void setMicGain(qreal g);
    bool vadEnabled() const;
    void setVadEnabled(bool on);
    qreal autoSendSecs() const;        // 0 = manual send only
    void setAutoSendSecs(qreal secs);
    QString llmModel() const;
    void setLlmModel(const QString &m);
    QString llmEndpoint() const;
    void setLlmEndpoint(const QString &e);

    QObject *updater() const;

    // Exposed to the settings UI.
    Q_INVOKABLE QVariantList modelList() const;   // [{key,label}]
    Q_INVOKABLE QVariantList styleList() const;    // builtins + custom presets
    Q_INVOKABLE QStringList backendList() const;
    Q_INVOKABLE QStringList presetNames() const;
    Q_INVOKABLE QString presetPrompt(const QString &name) const;
    Q_INVOKABLE double presetTemp(const QString &name) const;
    Q_INVOKABLE bool savePreset(const QString &name, const QString &prompt,
                                double temp = 0.3);
    Q_INVOKABLE void deletePreset(const QString &name);
    // AI helpers for the preset editor (results arrive via the signals below).
    Q_INVOKABLE void generatePreset(const QString &description);
    Q_INVOKABLE void testPreset(const QString &prompt, double temp,
                                const QString &sample);

    // Microphone settings UI.
    Q_INVOKABLE QVariantList micDeviceList() const;   // [{key,label}], key="" = system default
    Q_INVOKABLE void startMicPreview();               // live level meter while Idle
    Q_INVOKABLE void stopMicPreview();

    // Hardware / backend management for the settings UI.
    Q_INVOKABLE QVariantMap hardwareInfo() const;   // {nvidia,intel,amd,gpus,cpu}
    Q_INVOKABLE QVariantList backendInfo();         // per-backend availability (cached)
    Q_INVOKABLE void probeBackends(bool force = false);   // async; emits backendProbesChanged
    Q_INVOKABLE void installBackend(const QString &key);   // runs helper in a terminal

    // Ollama reachability + installed models for the settings UI.
    Q_INVOKABLE QVariantMap llmProbeInfo() const;   // {checking,available,models:[...]}
    Q_INVOKABLE void probeLlm(bool force = false);  // async; emits llmProbeChanged

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
    void themeChanged();
    void backendChanged();
    void languageChanged();
    void settingsOpenChanged();
    void presetsChanged();
    void presetDraftReady(const QString &text);     // generatePreset result
    void presetDraftFailed(const QString &message);
    void presetTestReady(const QString &text);      // testPreset result
    void presetTestFailed(const QString &message);
    void modelChanged();
    void micDeviceChanged();
    void micGainChanged();
    void vadEnabledChanged();
    void autoSendSecsChanged();
    void llmModelChanged();
    void llmEndpointChanged();
    void requestShow();
    void requestHide();
    void notify(const QString &message);   // non-fatal user-facing messages
    void backendProbesChanged();           // an async availability probe finished
    void llmProbeChanged();                // an async Ollama reachability probe finished

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
    Updater *m_updater = nullptr;
    QString m_language = QStringLiteral("auto");
    QString m_model = QStringLiteral("small");
    QString m_theme = QStringLiteral("oled");
    QString m_beautifyStyle = QStringLiteral("format");
    bool m_settingsOpen = false;

    QTimer *m_partialTimer = nullptr;
    QTimer *m_unloadTimer = nullptr;   // unloads the model after idle
    QTimer *m_autoSendTimer = nullptr; // watches for post-speech silence
    bool m_sttBusy = false;   // a transcription is in flight
    bool m_cancelled = false; // ignore results after a cancel
    bool m_modelReady = false;
    bool m_modelLoading = false;
    bool m_previewEnabled = true;
    bool m_partialTruncated = false;   // preview covered only the tail window

    // Async backend availability probes (settings "Hardware" pane).
    QNetworkAccessManager *m_probeNam = nullptr;
    int m_fasterWhisperAvail = -1;   // -1 unknown, 0 no, 1 yes (cached)
    int m_whisperCppAvail = -1;      // -1 unknown, 0 no, 1 yes (cached)
    bool m_probingPython = false;
    bool m_probingWhisperCpp = false;

    // Async Ollama reachability + installed-model probe (settings "Formatting" pane).
    QNetworkAccessManager *m_llmNam = nullptr;
    QStringList m_llmModels;
    int m_llmAvail = -1;   // -1 unknown, 0 unreachable, 1 reachable
    bool m_probingLlm = false;
};
