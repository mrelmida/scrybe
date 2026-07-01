#pragma once

#include "SttBackend.h"

// whisper.cpp backend, talking to a running `whisper-server` over HTTP. The
// server's device is decided at its build time (Vulkan / CUDA / Metal / CPU),
// so this backend works with any GPU whisper.cpp supports. `device` is only
// informational here; the endpoint is read from settings (whispercpp/endpoint).
class WhisperCppBackend : public ISttBackend {
public:
    WhisperCppBackend();

    bool load(const QString &model, const QString &device,
              QString *effectiveDevice, QString *err) override;
    void unload() override {}
    bool transcribe(const std::vector<float> &pcm16k, const QString &language,
                    QString *text, QString *err) override;
    QString name() const override { return QStringLiteral("whispercpp"); }

private:
    QString m_endpoint;
};
