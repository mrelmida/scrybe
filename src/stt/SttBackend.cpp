#include "SttBackend.h"

#include "FasterWhisperBackend.h"
#include "OpenVinoBackend.h"
#include "WhisperCppBackend.h"

std::unique_ptr<ISttBackend> makeSttBackend(const QString &type) {
    if (type == QLatin1String("faster-whisper"))
        return std::make_unique<FasterWhisperBackend>();
    if (type == QLatin1String("whispercpp"))
        return std::make_unique<WhisperCppBackend>();
    if (type == QLatin1String("openvino"))
        return std::make_unique<OpenVinoBackend>();
    return nullptr;
}
