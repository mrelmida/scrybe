#include "OpenVinoBackend.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <openvino/genai/whisper_pipeline.hpp>

#include <exception>
#include <filesystem>

OpenVinoBackend::OpenVinoBackend() = default;
OpenVinoBackend::~OpenVinoBackend() = default;

bool OpenVinoBackend::load(const QString &model, const QString &device,
                           QString *effectiveDevice, QString *err) {
    if (!QFileInfo::exists(model)) {
        *err = QStringLiteral("Whisper model not found at %1.").arg(model);
        return false;
    }
    try {
        const std::filesystem::path path(model.toStdString());

        // Cache compiled kernels so subsequent loads (esp. GPU) are fast.
        const QString cacheDir =
            QDir(QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation))
                .filePath(QStringLiteral("scrybe/ov_cache"));
        QDir().mkpath(cacheDir);
        ov::AnyMap props;
        props["CACHE_DIR"] = cacheDir.toStdString();

        m_pipe = std::make_unique<ov::genai::WhisperPipeline>(
            path, device.toStdString(), props);
        *effectiveDevice = device;
        return true;
    } catch (const std::exception &e) {
        m_pipe.reset();
        *err = QStringLiteral("OpenVINO load failed on %1: %2")
                   .arg(device, QString::fromUtf8(e.what()));
        return false;
    }
}

void OpenVinoBackend::unload() { m_pipe.reset(); }

bool OpenVinoBackend::transcribe(const std::vector<float> &pcm16k,
                                 const QString &language, QString *text,
                                 QString *err) {
    if (!m_pipe) {
        *err = QStringLiteral("OpenVINO model is not loaded.");
        return false;
    }
    try {
        auto cfg = m_pipe->get_generation_config();
        cfg.task = "transcribe";
        if (!language.isEmpty() && language != QLatin1String("auto"))
            cfg.language = "<|" + language.toStdString() + "|>";

        ov::genai::WhisperDecodedResults res = m_pipe->generate(pcm16k, cfg);
        *text = res.texts.empty()
                    ? QString()
                    : QString::fromStdString(res.texts.front()).trimmed();
        return true;
    } catch (const std::exception &e) {
        *err = QStringLiteral("OpenVINO transcription failed: %1")
                   .arg(QString::fromUtf8(e.what()));
        return false;
    }
}
