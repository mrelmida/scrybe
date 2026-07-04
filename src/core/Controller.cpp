#include "Controller.h"

#include "audio/AudioCapture.h"
#include "llm/LlmBeautifier.h"
#include "paste/Paster.h"
#include "stt/Models.h"
#include "stt/SttEngine.h"
#include "update/Updater.h"
#include "util/Terminal.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#ifndef SCRYBE_HAVE_OPENVINO
#define SCRYBE_HAVE_OPENVINO 0
#endif

namespace {
constexpr int kPartialIntervalMs = 900;   // how often to refresh live text
constexpr double kMinPartialSecs = 0.5;   // don't transcribe less than this
constexpr int kUnloadIdleMs = 20000;      // unload model after 20s idle
constexpr int kPartialWindowSecs = 25;    // preview transcribes at most this much

// Marker written after a model download completes. `snapshot_download` creates
// the directory before it finishes, so the directory alone doesn't prove the
// files are all there (an interrupted download would look "installed" forever).
constexpr char kCompleteMarker[] = ".complete";

QString modelsRoot() {
    // ~/.local/share/scrybe/models  (matches scripts/download-model.sh)
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        .filePath(QStringLiteral("scrybe/models"));
}

// Detected GPU vendors. Read from sysfs PCI vendor ids (no external tools).
struct Gpus {
    bool nvidia = false;
    bool intel = false;
    bool amd = false;
    QStringList names;   // human-readable labels
};

Gpus detectGpus() {
    Gpus g;
    if (QFileInfo::exists(QStringLiteral("/proc/driver/nvidia/version")) ||
        !QStandardPaths::findExecutable(QStringLiteral("nvidia-smi")).isEmpty())
        g.nvidia = true;

    // Each render node exposes its PCI vendor id at device/vendor.
    const QDir drm(QStringLiteral("/sys/class/drm"));
    const auto cards = drm.entryList(QStringList{QStringLiteral("card[0-9]*")},
                                     QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &card : cards) {
        if (card.contains(QLatin1Char('-')))   // skip connector nodes (card0-HDMI…)
            continue;
        QFile vf(drm.filePath(card + QStringLiteral("/device/vendor")));
        if (!vf.open(QIODevice::ReadOnly))
            continue;
        const QString v = QString::fromLatin1(vf.readAll()).trimmed().toLower();
        if (v == QLatin1String("0x8086")) g.intel = true;
        else if (v == QLatin1String("0x10de")) g.nvidia = true;
        else if (v == QLatin1String("0x1002")) g.amd = true;
    }
    if (g.nvidia) g.names << QStringLiteral("NVIDIA (CUDA)");
    if (g.intel)  g.names << QStringLiteral("Intel (iGPU/Arc)");
    if (g.amd)    g.names << QStringLiteral("AMD (Vulkan/ROCm)");
    return g;
}
} // namespace

Controller::Controller(QObject *parent) : QObject(parent) {
    m_audio = new AudioCapture(this);
    connect(m_audio, &AudioCapture::levelChanged, this, &Controller::setLevel);
    connect(m_audio, &AudioCapture::error, this,
            [this](const QString &msg) { emit notify(msg); });
    connect(m_audio, &AudioCapture::limitReached, this, [this]() {
        emit notify(tr("Recording limit reached — finalizing."));
        stopListening();
    });

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
        emit notify(tr("Speech model ready (%1).").arg(dev));
    });

    m_paster = new Paster(this);
    connect(m_paster, &Paster::error, this,
            [this](const QString &msg) { emit notify(msg); });

    m_updater = new Updater(this);
    connect(m_updater, &Updater::notify, this,
            [this](const QString &msg) { emit notify(msg); });

    m_llm = new LlmBeautifier(this);
    connect(m_llm, &LlmBeautifier::done, this, [this](const QString &text) {
        if (m_state != Beautifying || m_cancelled) return;
        setTranscript(text);
        finish();
    });
    connect(m_llm, &LlmBeautifier::failed, this, [this](const QString &msg) {
        if (m_state != Beautifying || m_cancelled) return;
        emit notify(msg + tr(" — pasting raw text."));
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
    m_theme = s.value(QStringLiteral("ui/theme"), QStringLiteral("oled")).toString();
    m_language = s.value(QStringLiteral("stt/language"), QStringLiteral("auto")).toString();
    m_previewEnabled = s.value(QStringLiteral("ui/preview"), true).toBool();
    m_beautifyStyle = s.value(QStringLiteral("llm/style"), QStringLiteral("format")).toString();
    m_llmEnabled = s.value(QStringLiteral("llm/enabled"), false).toBool();
}

QString Controller::device() const {
    return QSettings().value(QStringLiteral("stt/device"),
                             QStringLiteral("AUTO:GPU,CPU")).toString();
}

// Resolve the STT backend. "auto" picks the best engine for the detected
// hardware: NVIDIA → faster-whisper (CUDA); Intel → OpenVINO (iGPU/Arc/NPU),
// when compiled in; anything else → faster-whisper on CPU (self-contained).
// whisper.cpp (Vulkan) stays an explicit choice since it needs a running server.
QString Controller::activeBackend() const {
    const QString b = QSettings().value(QStringLiteral("stt/backend"),
                                         QStringLiteral("auto")).toString();
    if (b != QLatin1String("auto"))
        return b;
    const Gpus g = detectGpus();
    if (g.nvidia)
        return QStringLiteral("faster-whisper");
#if SCRYBE_HAVE_OPENVINO
    if (g.intel)
        return QStringLiteral("openvino");
#endif
    return QStringLiteral("faster-whisper");   // CPU / AMD fallback
}

QString Controller::modelDirFor(const QString &key) const {
    return QDir(modelsRoot()).filePath(scrybe::modelSubdir(key));
}

// Ensure the model files exist on disk (downloading in the background if not),
// then invoke cb(success). cb runs immediately if already present. A directory
// without the completion marker is a previously interrupted download; the
// snapshot download resumes it.
void Controller::ensureDownloaded(const QString &key, std::function<void(bool)> cb) {
    const QString dir = modelDirFor(key);
    const QString marker = QDir(dir).filePath(QString::fromLatin1(kCompleteMarker));
    if (QFileInfo::exists(marker)) {
        cb(true);
        return;
    }
    // Pre-marker installs: a dir that already holds the IR files is complete —
    // adopt it rather than forcing a re-download (which would fail offline).
    const QDir d(dir);
    if (d.exists() &&
        !d.entryList({QStringLiteral("*.xml")}, QDir::Files).isEmpty() &&
        !d.entryList({QStringLiteral("*.bin")}, QDir::Files).isEmpty()) {
        QFile f(marker);
        if (f.open(QIODevice::WriteOnly))
            f.write(QByteArray("adopted existing download\n"));
        cb(true);
        return;
    }
    emit notify(tr("Downloading model '%1'…").arg(key));
    auto *p = new QProcess(this);
    p->setProgram(QStringLiteral("python3"));
    // Repo and path travel as argv, not interpolated into the code string.
    p->setArguments({QStringLiteral("-c"),
        QStringLiteral("import sys\n"
                       "from huggingface_hub import snapshot_download\n"
                       "snapshot_download(repo_id=sys.argv[1], local_dir=sys.argv[2],"
                       "allow_patterns=['*.xml','*.bin','*.json','*.txt'])"),
        scrybe::modelRepo(key), dir});
    connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, p, dir, marker, key, cb](int code, QProcess::ExitStatus st) {
                p->deleteLater();
                const bool ok = (st == QProcess::NormalExit && code == 0 &&
                                 QFileInfo::exists(dir));
                if (ok) {
                    QFile f(marker);
                    if (f.open(QIODevice::WriteOnly))
                        f.write(QByteArray("downloaded by scrybe\n"));
                    emit notify(tr("Model '%1' downloaded.").arg(key));
                } else {
                    emit notify(tr("Download failed for '%1'.").arg(key));
                }
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
    emit notify(tr("Model set to '%1' (loads on next recording).").arg(key));
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

void Controller::setTheme(const QString &t) {
    if (m_theme == t) return;
    m_theme = t;
    QSettings().setValue(QStringLiteral("ui/theme"), t);
    emit themeChanged();
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
    emit notify(tr("Backend set to '%1' (applies on next recording).").arg(b));
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

QObject *Controller::updater() const { return m_updater; }

QVariantMap Controller::hardwareInfo() const {
    const Gpus g = detectGpus();
    return QVariantMap{
        {QStringLiteral("nvidia"), g.nvidia},
        {QStringLiteral("intel"), g.intel},
        {QStringLiteral("amd"), g.amd},
        {QStringLiteral("gpus"), g.names.isEmpty()
             ? QStringLiteral("No discrete/integrated GPU detected — CPU only")
             : g.names.join(QStringLiteral(", "))},
        {QStringLiteral("resolved"), activeBackend()},
    };
}

// Per-backend availability for the settings "Hardware" pane. Purely reads the
// cached probe results — call probeBackends() to refresh them asynchronously.
QVariantList Controller::backendInfo() {
    const Gpus g = detectGpus();

    QVariantList out;

    // OpenVINO (Intel). "available" means it was compiled into this binary.
    out.append(QVariantMap{
        {QStringLiteral("key"), QStringLiteral("openvino")},
        {QStringLiteral("label"), QStringLiteral("OpenVINO — Intel iGPU / Arc / NPU + CPU")},
        {QStringLiteral("available"), bool(SCRYBE_HAVE_OPENVINO)},
        {QStringLiteral("recommended"), g.intel && !g.nvidia},
        {QStringLiteral("installable"), !bool(SCRYBE_HAVE_OPENVINO)},
        {QStringLiteral("note"), bool(SCRYBE_HAVE_OPENVINO)
             ? tr("Installed and ready.")
             : tr("Not built in. Install to enable Intel GPU acceleration.")},
    });

    // faster-whisper (NVIDIA / CPU): needs the Python package (CTranslate2).
    out.append(QVariantMap{
        {QStringLiteral("key"), QStringLiteral("faster-whisper")},
        {QStringLiteral("label"), QStringLiteral("faster-whisper — NVIDIA (CUDA) + CPU")},
        {QStringLiteral("available"), m_fasterWhisperAvail == 1},
        {QStringLiteral("recommended"), g.nvidia},
        {QStringLiteral("installable"), true},
        {QStringLiteral("note"),
         m_fasterWhisperAvail == 1 ? tr("Installed and ready.")
         : m_fasterWhisperAvail < 0 ? tr("Checking…")
                                    : tr("Python package not found. Install to enable.")},
    });

    // whisper.cpp (Vulkan / any GPU). Needs a server the user runs.
    out.append(QVariantMap{
        {QStringLiteral("key"), QStringLiteral("whispercpp")},
        {QStringLiteral("label"), QStringLiteral("whisper.cpp — Vulkan / AMD / any GPU")},
        {QStringLiteral("available"), m_whisperCppAvail == 1},
        {QStringLiteral("recommended"), g.amd && !g.nvidia && !g.intel},
        {QStringLiteral("installable"), true},
        {QStringLiteral("note"),
         m_whisperCppAvail == 1 ? tr("whisper-server is running and reachable.")
         : m_whisperCppAvail < 0 ? tr("Checking…")
                                 : tr("Builds a local server (Vulkan). Best for AMD GPUs.")},
    });

    return out;
}

// Kick off the availability probes that would block the GUI if run inline
// (python interpreter start-up, HTTP reachability). Results land in the caches
// and each completion emits backendProbesChanged().
void Controller::probeBackends(bool force) {
    if (force) {
        if (!m_probingPython)     m_fasterWhisperAvail = -1;
        if (!m_probingWhisperCpp) m_whisperCppAvail = -1;
    }

    // faster-whisper: find_spec only checks presence — it does NOT import the
    // (slow) module — but even starting python3 can stall, hence async.
    if (m_fasterWhisperAvail < 0 && !m_probingPython) {
        m_probingPython = true;
        auto *p = new QProcess(this);
        connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, p](int code, QProcess::ExitStatus st) {
                    p->deleteLater();
                    m_probingPython = false;
                    m_fasterWhisperAvail =
                        (st == QProcess::NormalExit && code == 0) ? 1 : 0;
                    emit backendProbesChanged();
                });
        connect(p, &QProcess::errorOccurred, this,
                [this, p](QProcess::ProcessError e) {
                    if (e != QProcess::FailedToStart)
                        return;
                    p->deleteLater();
                    m_probingPython = false;
                    m_fasterWhisperAvail = 0;
                    emit backendProbesChanged();
                });
        p->start(QStringLiteral("python3"),
                 {QStringLiteral("-c"),
                  QStringLiteral("import importlib.util,sys;"
                                 "sys.exit(0 if importlib.util.find_spec('faster_whisper') else 1)")});
    }

    // whisper.cpp: reachable server at the configured endpoint.
    if (m_whisperCppAvail < 0 && !m_probingWhisperCpp) {
        m_probingWhisperCpp = true;
        if (!m_probeNam)
            m_probeNam = new QNetworkAccessManager(this);
        const QString endpoint = QSettings()
            .value(QStringLiteral("whispercpp/endpoint"),
                   QStringLiteral("http://127.0.0.1:8080")).toString();
        QNetworkRequest req{QUrl(endpoint)};
        req.setTransferTimeout(2000);
        QNetworkReply *reply = m_probeNam->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            reply->deleteLater();
            m_probingWhisperCpp = false;
            m_whisperCppAvail =
                (reply->error() == QNetworkReply::NoError ||
                 reply->error() == QNetworkReply::ContentNotFoundError) ? 1 : 0;
            emit backendProbesChanged();
        });
    }
}

// Install a backend on demand by re-running the setup helper in a terminal so
// the user can watch progress and answer sudo prompts. Invalidates caches.
void Controller::installBackend(const QString &key) {
    const QString src = QDir(QStandardPaths::writableLocation(
                                 QStandardPaths::HomeLocation))
                            .filePath(QStringLiteral(".local/src/scrybe"));
    const QString helper = QDir(src).filePath(QStringLiteral("scripts/install-backend.sh"));

    QString cmd;
    if (QFileInfo::exists(helper))
        cmd = QStringLiteral("bash %1 %2").arg(helper, key);
    else
        cmd = QStringLiteral(
            "curl -fsSL https://raw.githubusercontent.com/mrelmida/scrybe/main/"
            "scripts/install-backend.sh | bash -s -- %1").arg(key);

    m_fasterWhisperAvail = -1;   // re-probe next time the pane opens
    m_whisperCppAvail = -1;
    if (scrybe::launchTerminal(cmd))
        emit notify(tr("Installing '%1' in a terminal…").arg(key));
    else
        emit notify(tr("No terminal found. Install '%1' via build-and-setup.sh.").arg(key));
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
    m_partialTruncated = false;
    m_stt->setDropPartials(false);
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
    const QVector<float> &pcm = m_audio->pcm();
    if (rate <= 0 || pcm.size() < int(kMinPartialSecs * rate))
        return;
    // Long dictations: preview only the most recent window so the per-tick cost
    // stays bounded (the final pass at stop still uses the full buffer).
    const qsizetype maxSamples = qsizetype(kPartialWindowSecs) * rate;
    m_partialTruncated = pcm.size() > maxSamples;
    m_sttBusy = true;
    m_stt->transcribe(m_partialTruncated ? pcm.mid(pcm.size() - maxSamples) : pcm,
                      rate, m_language, /*isFinal=*/false);
}

void Controller::stopListening() {
    if (m_state != Listening) return;
    m_partialTimer->stop();
    m_stt->setDropPartials(true);   // a queued preview must not delay the final
    m_audio->stop();
    setLevel(0.0);

    const QVector<float> pcm = m_audio->pcm();
    const int rate = m_audio->sampleRate();
    if (rate > 0 && pcm.size() < rate / 4) {   // < 0.25s captured
        emit notify(tr("Too short — nothing captured."));
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
    m_stt->setDropPartials(true);
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
        // A leading ellipsis marks a preview that covers only the recent window.
        if (m_state == Listening && !text.isEmpty())
            setTranscript(m_partialTruncated ? QStringLiteral("… ") + text : text);
        return;
    }

    if (m_state != Transcribing)
        return;
    if (text.isEmpty()) {
        emit notify(tr("No speech recognized."));
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
