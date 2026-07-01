#pragma once

#include <QString>
#include <QVector>

// Central registry of selectable OpenVINO Whisper models (pre-converted IR on
// Hugging Face under the OpenVINO org). Keep in sync with scripts/download-model.sh.
namespace scrybe {

struct ModelInfo {
    QString key;      // settings value / CLI arg
    QString subdir;   // on-disk dir + HF repo basename
    QString label;    // menu label
};

inline QVector<ModelInfo> models() {
    return {
        {"tiny",     "whisper-tiny-fp16-ov",             "Tiny (fastest, low accuracy)"},
        {"base",     "whisper-base-fp16-ov",             "Base (fast)"},
        {"small",    "whisper-small-fp16-ov",            "Small (balanced)"},
        {"medium",   "whisper-medium-fp16-ov",           "Medium (accurate)"},
        {"turbo",    "whisper-large-v3-turbo-fp16-ov",   "Large-v3 Turbo (best speed/accuracy)"},
        {"large-v3", "whisper-large-v3-fp16-ov",         "Large-v3 (most accurate, slow)"},
        {"distil",   "distil-whisper-large-v3-fp16-ov",  "Distil-Large-v3 (fast, English)"},
    };
}

inline QString modelSubdir(const QString &key) {
    for (const auto &m : models())
        if (m.key == key)
            return m.subdir;
    return QStringLiteral("whisper-small-fp16-ov");
}

inline QString modelRepo(const QString &key) {
    return QStringLiteral("OpenVINO/") + modelSubdir(key);
}

// faster-whisper (CTranslate2) model id for a given key. faster-whisper resolves
// these names/repos and downloads them itself.
inline QString fasterWhisperModel(const QString &key) {
    if (key == "tiny")     return QStringLiteral("tiny");
    if (key == "base")     return QStringLiteral("base");
    if (key == "medium")   return QStringLiteral("medium");
    if (key == "turbo")    return QStringLiteral("large-v3-turbo");
    if (key == "large-v3") return QStringLiteral("large-v3");
    if (key == "distil")   return QStringLiteral("distil-large-v3");
    return QStringLiteral("small");
}

} // namespace scrybe
