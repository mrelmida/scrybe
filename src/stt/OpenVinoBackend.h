#pragma once

#include "SttBackend.h"

#include <memory>

namespace ov {
namespace genai {
class WhisperPipeline;
}
} // namespace ov

// OpenVINO GenAI Whisper backend — Intel iGPU / Arc / NPU / CPU.
// `model` is a directory of OpenVINO IR; `device` is an OpenVINO device string
// (e.g. "AUTO:GPU,CPU", "GPU", "CPU", "NPU").
class OpenVinoBackend : public ISttBackend {
public:
    OpenVinoBackend();
    ~OpenVinoBackend() override;

    bool load(const QString &model, const QString &device,
              QString *effectiveDevice, QString *err) override;
    void unload() override;
    bool transcribe(const std::vector<float> &pcm16k, const QString &language,
                    QString *text, QString *err) override;
    QString name() const override { return QStringLiteral("openvino"); }

private:
    std::unique_ptr<ov::genai::WhisperPipeline> m_pipe;
};
