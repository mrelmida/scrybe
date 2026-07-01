#include "Controller.h"

#include "audio/AudioCapture.h"
#include "llm/LlmBeautifier.h"
#include "paste/Paster.h"
#include "stt/Models.h"
#include "stt/SttEngine.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

namespace {
constexpr int kPartialIntervalMs = 900;   // how often to refresh live text
constexpr double kMinPartialSecs = 0.5;   // don't transcribe less than this
constexpr int kUnloadIdleMs = 20000;      // unload model after 20s idle

QString modelsRoot() {
    // ~/.local/share/scrybe/models  (matches scripts/download-model.sh)
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        .filePath(QStringLiteral("scrybe/models"));
}
} // namespace

Controller::Controller(QObject *parent) : QObject(parent) {
    m_audio = new AudioCapture(this);
    connect(m_audio, &AudioCapture::levelChanged, this, &Controller::setLevel);
    connect(m_audio, &AudioCapture::error, this,
            [this](const QString &msg) { emit notify(msg); });

    m_stt = new SttEngine(this);
    connect(m_stt, &SttEngine::transcript, this,
            [this](const QString &text, const QString &, bool isFinal) {
                onTranscript(text, isFinal);
            });
    connect(m_stt, &SttEngine::error, this, [this](const QString &msg) {
        m_modelLoading = false;
        emit notify(msg);
        if (m_state == Transcribing || m_state == Beautifying) {
            setState(Idle);
            emit requestHide();
        }
    });
    connect(m_stt, &SttEngine::ready, this, [this](const QString &dev) {
        m_modelReady = true;
        m_modelLoading = false;
        emit modelReadyChanged();
        emit notify(QStringLiteral("Speech model ready (%1).").arg(dev));
    });

    m_paster = new Paster(this);
    connect(m_paster, &Paster::error, this,
            [this](const QString &msg) { emit notify(msg); });

    m_llm = new LlmBeautifier(this);
    connect(m_llm, &LlmBeautifier::done, this, [this](const QString &text) {
        if (m_state != Beautifying || m_cancelled) return;
        setTranscript(text);
        finish();
    });
    connect(m_llm, &LlmBeautifier::failed, this, [this](const QString &msg) {
        if (m_state != Beautifying || m_cancelled) return;
        emit notify(msg + QStringLiteral(" — pasting raw text."));
        finish();   // m_transcript still holds the raw text
    });

    m_partialTimer = new QTimer(this);
    m_partialTimer->setInterval(kPartialIntervalMs);
    connect(m_partialTimer, &QTimer::timeout, this, &Controller::requestPartial);

    // Unload the model after a grace period of inactivity to free the iGPU/RAM.
    m_unloadTimer = new QTimer(this);
    m_unloadTimer->setSingleShot(true);
    m_unloadTimer->setInterval(kUnloadIdleMs);
    connect(m_unloadTimer, &QTimer::timeout, this, [this]() {
        if (m_state == Idle && (m_modelReady || m_modelLoading)) {
            m_stt->unload();
            m_modelReady = false;
            m_modelLoading = false;
            emit modelReadyChanged();
        }
    });

    // The model is loaded lazily when recording starts (not at launch).
    QSettings s;
    m_model = s.value(QStringLiteral("stt/model"), QStringLiteral("small")).toString();
    m_language = s.value(QStringLiteral("stt/language"), QStringLiteral("auto")).toString();
    m_previewEnabled = s.value(QStringLiteral("ui/preview"), true).toBool();
    m_beautifyStyle = s.value(QStringLiteral("llm/style"), QStringLiteral("format")).toString();
    m_llmEnabled = s.value(QStringLiteral("llm/enabled"), false).toBool();
}

QString Controller::device() const {
    return QSettings().value(QStringLiteral("stt/device"),
                             QStringLiteral("AUTO:GPU,CPU")).toString();
}

// Resolve the STT backend. "auto" picks faster-whisper if an NVIDIA GPU is
// present, otherwise OpenVINO (best on Intel iGPU/Arc/NPU + CPU).
QString Controller::activeBackend() const {
    const QString b = QSettings().value(QStringLiteral("stt/backend"),
                                         QStringLiteral("auto")).toString();
    if (b != QLatin1String("auto"))
        return b;
    const bool nvidia = QFileInfo::exists(QStringLiteral("/proc/driver/nvidia/version")) ||
                        !QStandardPaths::findExecutable(QStringLiteral("nvidia-smi")).isEmpty();
    return nvidia ? QStringLiteral("faster-whisper") : QStringLiteral("openvino");
}

QString Controller::modelDirFor(const QString &key) const {
    return QDir(modelsRoot()).filePath(scrybe::modelSubdir(key));
}

// Ensure the model files exist on disk (downloading in the background if not),
// then invoke cb(success). cb runs immediately if already present.
void Controller::ensureDownloaded(const QString &key, std::function<void(bool)> cb) {
    const QString dir = modelDirFor(key);
    if (QFileInfo::exists(dir)) {
        cb(true);
        return;
    }
    emit notify(QStringLiteral("Downloading model '%1'…").arg(key));
    auto *p = new QProcess(this);
    p->setProgram(QStringLiteral("python3"));
    p->setArguments({QStringLiteral("-c"),
        QStringLiteral("from huggingface_hub import snapshot_download;"
                       "snapshot_download(repo_id='%1', local_dir='%2',"
                       "allow_patterns=['*.xml','*.bin','*.json','*.txt'])")
            .arg(scrybe::modelRepo(key), dir)});
    connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, p, dir, key, cb](int code, QProcess::ExitStatus) {
                p->deleteLater();
                const bool ok = (code == 0 && QFileInfo::exists(dir));
                if (ok) emit notify(QStringLiteral("Model '%1' downloaded.").arg(key));
                else    emit notify(QStringLiteral("Download failed for '%1'.").arg(key));
                cb(ok);
            });
    p->start();
}

// Load the current model into memory if it isn't already (called at record start
// so loading overlaps with the user speaking).
void Controller::ensureModelLoaded() {
    if (m_modelReady || m_modelLoading)
        return;
    m_modelLoading = true;
    const QString backend = activeBackend();
    const QString key = m_model;

    if (backend == QLatin1String("openvino")) {
        // OpenVINO uses pre-converted IR on disk; fetch it if missing.
        ensureDownloaded(key, [this, backend, key](bool ok) {
            if (!ok) { m_modelLoading = false; return; }
            m_stt->load(backend, modelDirFor(key), device());
        });
    } else if (backend == QLatin1String("faster-whisper")) {
        // The sidecar downloads the CT2 model itself.
        m_stt->load(backend, scrybe::fasterWhisperModel(key), device());
    } else { // whispercpp: the server owns the model
        m_stt->load(backend, key, device());
    }
}

void Controller::scheduleUnload() {
    m_unloadTimer->start();   // fires only if still Idle after the grace period
}

void Controller::setModel(const QString &key) {
    if (key == m_model)
        return;
    m_model = key;
    QSettings().setValue(QStringLiteral("stt/model"), key);
    // Drop the old model; the new one loads on the next recording.
    m_stt->unload();
    m_modelReady = false;
    m_modelLoading = false;
    emit modelReadyChanged();
    emit modelChanged();
    emit notify(QStringLiteral("Model set to '%1' (loads on next recording).").arg(key));
    if (activeBackend() == QLatin1String("openvino"))
        ensureDownloaded(key, [](bool) {});   // pre-fetch OpenVINO IR
}

void Controller::setPreviewEnabled(bool on) {
    if (m_previewEnabled == on) return;
    m_previewEnabled = on;
    QSettings().setValue(QStringLiteral("ui/preview"), on);
    emit previewEnabledChanged();
}

QString Controller::stateName() const {
    switch (m_state) {
    case Idle:         return QStringLiteral("idle");
    case Listening:    return QStringLiteral("listening");
    case Transcribing: return QStringLiteral("transcribing");
    case Beautifying:  return QStringLiteral("beautifying");
    case Pasting:      return QStringLiteral("pasting");
    }
    return QStringLiteral("idle");
}

void Controller::setLlmEnabled(bool on) {
    if (m_llmEnabled == on) return;
    m_llmEnabled = on;
    QSettings().setValue(QStringLiteral("llm/enabled"), on);
    emit llmEnabledChanged();
}

void Controller::setBeautifyStyle(const QString &s) {
    if (m_beautifyStyle == s) return;
    m_beautifyStyle = s;
    QSettings().setValue(QStringLiteral("llm/style"), s);
    emit beautifyStyleChanged();
}

void Controller::setSettingsOpen(bool on) {
    if (m_settingsOpen == on) return;
    m_settingsOpen = on;
    emit settingsOpenChanged();
}

QString Controller::backend() const {
    return QSettings().value(QStringLiteral("stt/backend"),
                             QStringLiteral("auto")).toString();
}

void Controller::setBackend(const QString &b) {
    if (backend() == b) return;
    QSettings().setValue(QStringLiteral("stt/backend"), b);
    m_stt->unload();               // next recording loads via the new backend
    m_modelReady = false;
    m_modelLoading = false;
    emit modelReadyChanged();
    emit backendChanged();
    emit notify(QStringLiteral("Backend set to '%1' (applies on next recording).").arg(b));
}

QString Controller::language() const { return m_language; }

void Controller::setLanguage(const QString &l) {
    if (m_language == l) return;
    m_language = l;
    QSettings().setValue(QStringLiteral("stt/language"), l);
    emit languageChanged();
}

QVariantList Controller::modelList() const {
    QVariantList out;
    for (const auto &m : scrybe::models())
        out.append(QVariantMap{{QStringLiteral("key"), m.key},
                               {QStringLiteral("label"), m.label}});
    return out;
}

QVariantList Controller::styleList() const {
    QVariantList out;
    out.append(QVariantMap{{"key", "format"},   {"label", "Clean formatting"}});
    out.append(QVariantMap{{"key", "markdown"}, {"label", "Markdown (lists, headings)"}});
    out.append(QVariantMap{{"key", "summary"},  {"label", "Summarize & shorten"}});
    for (const QString &name : presetNames())
        out.append(QVariantMap{{"key", name},
                               {"label", name + QStringLiteral(" (custom)")}});
    return out;
}

QStringList Controller::backendList() const {
    return {QStringLiteral("auto"), QStringLiteral("openvino"),
            QStringLiteral("faster-whisper"), QStringLiteral("whispercpp")};
}

QStringList Controller::presetNames() const {
    QSettings s;
    s.beginGroup(QStringLiteral("presets"));
    return s.childKeys();
}

QString Controller::presetPrompt(const QString &name) const {
    return QSettings().value(QStringLiteral("presets/") + name).toString();
}

void Controller::savePreset(const QString &name, const QString &prompt) {
    const QString n = name.trimmed();
    if (n.isEmpty()) return;
    QSettings().setValue(QStringLiteral("presets/") + n, prompt);
    emit presetsChanged();
}

void Controller::deletePreset(const QString &name) {
    QSettings().remove(QStringLiteral("presets/") + name);
    if (m_beautifyStyle == name)
        setBeautifyStyle(QStringLiteral("format"));
    emit presetsChanged();
}

void Controller::toggle() {
    if (m_state == Idle) startListening();
    else if (m_state == Listening) stopListening();
}

void Controller::startListening() {
    if (m_state != Idle) return;
    m_cancelled = false;
    m_sttBusy = false;
    m_unloadTimer->stop();
    setTranscript(QString());
    setState(Listening);
    ensureModelLoaded();          // load now — overlaps with the user speaking
    m_audio->start();
    if (m_previewEnabled)
        m_partialTimer->start();  // live preview only when enabled
    emit requestShow();
}

// Rolling partial transcription so text appears live while speaking.
void Controller::requestPartial() {
    if (m_state != Listening || m_sttBusy || !m_previewEnabled)
        return;
    const int rate = m_audio->sampleRate();
    const QVector<float> pcm = m_audio->pcm();
    if (rate <= 0 || pcm.size() < int(kMinPartialSecs * rate))
        return;
    m_sttBusy = true;
    m_stt->transcribe(pcm, rate, m_language, /*isFinal=*/false);
}

void Controller::stopListening() {
    if (m_state != Listening) return;
    m_partialTimer->stop();
    m_audio->stop();
    setLevel(0.0);

    const QVector<float> pcm = m_audio->pcm();
    const int rate = m_audio->sampleRate();
    if (rate > 0 && pcm.size() < rate / 4) {   // < 0.25s captured
        emit notify(QStringLiteral("Too short — nothing captured."));
        setState(Idle);
        emit requestHide();
        scheduleUnload();
        return;
    }
    setState(Transcribing);
    m_sttBusy = true;
    m_stt->transcribe(pcm, rate, m_language, /*isFinal=*/true);
}

void Controller::send() {
    if (m_state == Listening)
        stopListening();
    // If already transcribing, we're effectively already sending.
}

void Controller::cancel() {
    if (m_state == Idle)
        return;
    m_partialTimer->stop();
    m_audio->stop();
    m_cancelled = true;   // ignore any in-flight STT result
    setLevel(0.0);
    setTranscript(QString());
    setState(Idle);
    emit requestHide();
    scheduleUnload();
}

void Controller::onTranscript(const QString &text, bool isFinal) {
    m_sttBusy = false;
    if (m_cancelled)
        return;

    if (!isFinal) {
        // Live partial: only update while still listening, and never blank out.
        if (m_state == Listening && !text.isEmpty())
            setTranscript(text);
        return;
    }

    if (m_state != Transcribing)
        return;
    if (text.isEmpty()) {
        emit notify(QStringLiteral("No speech recognized."));
        setState(Idle);
        emit requestHide();
        scheduleUnload();
        return;
    }
    setTranscript(text);
    if (m_llmEnabled) {
        setState(Beautifying);   // overlay shows "Transcribing…"/busy; then paste
        m_llm->beautify(text, m_beautifyStyle);
    } else {
        finish();
    }
}

void Controller::finish() {
    const QString text = m_transcript;
    setState(Pasting);
    // Hide the overlay first so it releases the keyboard grab and KWin returns
    // focus to the target app; then paste into it via the clipboard.
    emit requestHide();
    QTimer::singleShot(220, this, [this, text]() {
        if (!text.isEmpty())
            m_paster->paste(text);
        setState(Idle);
        scheduleUnload();
    });
}

void Controller::setState(State s) {
    if (m_state == s) return;
    m_state = s;
    emit stateChanged();
}

void Controller::setLevel(qreal v) {
    if (qFuzzyCompare(m_level, v)) return;
    m_level = v;
    emit levelChanged();
}

void Controller::setTranscript(const QString &t) {
    if (m_transcript == t) return;
    m_transcript = t;
    emit transcriptChanged();
}
