#include "SttBackend.h"

#include "FasterWhisperBackend.h"
#include "WhisperCppBackend.h"
#if SCRYBE_HAVE_OPENVINO
#include "OpenVinoBackend.h"
#endif

std::unique_ptr<ISttBackend> makeSttBackend(const QString &type) {
    if (type == QLatin1String("faster-whisper"))
        return std::make_unique<FasterWhisperBackend>();
    if (type == QLatin1String("whispercpp"))
        return std::make_unique<WhisperCppBackend>();
    if (type == QLatin1String("openvino")) {
#if SCRYBE_HAVE_OPENVINO
        return std::make_unique<OpenVinoBackend>();
#else
        return nullptr;   // built without OpenVINO (non-Intel machine)
#endif
    }
    return nullptr;
}
