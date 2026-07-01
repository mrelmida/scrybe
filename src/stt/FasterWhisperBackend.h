#pragma once

#include "SttBackend.h"

class QProcess;

// faster-whisper (CTranslate2) backend for NVIDIA CUDA and CPU. Runs a
// persistent Python sidecar (scripts/faster_whisper_sidecar.py) and talks to it
// over JSON/stdio. `model` is a size name or CT2 repo ("base", "large-v3",
// "large-v3-turbo", "distil-large-v3"); `device` is "cuda", "cpu" or "auto".
class FasterWhisperBackend : public ISttBackend {
public:
    FasterWhisperBackend();
    ~FasterWhisperBackend() override;

    bool load(const QString &model, const QString &device,
              QString *effectiveDevice, QString *err) override;
    void unload() override;
    bool transcribe(const std::vector<float> &pcm16k, const QString &language,
                    QString *text, QString *err) override;
    QString name() const override { return QStringLiteral("faster-whisper"); }

private:
    QString readLine(int timeoutMs, QString *err);

    QProcess *m_proc = nullptr;
};
