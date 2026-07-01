#pragma once

#include <QString>

#include <memory>
#include <vector>

// A speech-to-text backend. Implementations run on the STT worker thread, so
// their methods may block. Input audio is always 16 kHz mono float.
class ISttBackend {
public:
    virtual ~ISttBackend() = default;

    // Load a model onto a device. `model` and `device` are interpreted by the
    // backend (e.g. a directory for OpenVINO, a size name for faster-whisper).
    // Returns false and sets *err on failure. *effectiveDevice reports what was
    // actually used (e.g. "GPU", "cuda", "cpu").
    virtual bool load(const QString &model, const QString &device,
                      QString *effectiveDevice, QString *err) = 0;

    virtual void unload() = 0;

    virtual bool transcribe(const std::vector<float> &pcm16k,
                            const QString &language, QString *text,
                            QString *err) = 0;

    virtual QString name() const = 0;
};

// Creates a backend by type: "openvino" | "faster-whisper" | "whispercpp".
// Returns nullptr for an unknown type.
std::unique_ptr<ISttBackend> makeSttBackend(const QString &type);
