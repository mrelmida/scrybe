cmake_minimum_required(VERSION 3.21)
project(scrybe VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick Widgets Network Multimedia)

# M2: overlay anchoring (KWin/Wayland) + global hotkey registration.
find_package(LayerShellQt REQUIRED)
find_package(KF6GlobalAccel REQUIRED)

# M3: OpenVINO Whisper STT — OPTIONAL so Scrybe builds on any machine.
# Core OpenVINO comes from the wheel-extracted tree (headers + cmake + libs);
# GenAI ships only the runtime .so there, so we add its headers/lib manually.
# The backend is compiled in only when the GenAI C++ files are present, which
# makes sense on Intel iGPU/Arc + CPU. On NVIDIA/AMD-only machines the installer
# skips OpenVINO and the app falls back to faster-whisper / whisper.cpp.
set(SCRYBE_OV_ROOT "/opt/scrybe-openvino" CACHE PATH "OpenVINO install root")
set(SCRYBE_WITH_OPENVINO "AUTO" CACHE STRING "OpenVINO backend: AUTO | ON | OFF")

set(SCRYBE_OPENVINO_ENABLED FALSE)
if(NOT SCRYBE_WITH_OPENVINO STREQUAL "OFF")
    if(NOT OpenVINO_DIR)
        set(OpenVINO_DIR "${SCRYBE_OV_ROOT}/openvino/cmake")
    endif()
    find_package(OpenVINO QUIET COMPONENTS Runtime)
    set(SCRYBE_GENAI_INC "${SCRYBE_OV_ROOT}/openvino_genai/include")
    find_library(SCRYBE_GENAI_LIB NAMES openvino_genai
        PATHS "${SCRYBE_OV_ROOT}/openvino_genai" NO_DEFAULT_PATH)
    if(OpenVINO_FOUND AND SCRYBE_GENAI_LIB
       AND EXISTS "${SCRYBE_GENAI_INC}/openvino/genai/whisper_pipeline.hpp")
        set(SCRYBE_OPENVINO_ENABLED TRUE)
    elseif(SCRYBE_WITH_OPENVINO STREQUAL "ON")
        message(FATAL_ERROR
            "OpenVINO backend requested (SCRYBE_WITH_OPENVINO=ON) but the GenAI "
            "C++ files were not found under ${SCRYBE_OV_ROOT}.\n"
            "Run ./build-and-setup.sh (with an Intel GPU) to install them, or set "
            "SCRYBE_WITH_OPENVINO=OFF.")
    endif()
endif()

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(scrybe
    src/main.cpp
    src/core/Controller.h
    src/core/Controller.cpp
    src/audio/AudioCapture.h
    src/audio/AudioCapture.cpp
    src/stt/SttEngine.h
    src/stt/SttEngine.cpp
    src/stt/SttBackend.h
    src/stt/SttBackend.cpp
    src/stt/FasterWhisperBackend.h
    src/stt/FasterWhisperBackend.cpp
    src/stt/WhisperCppBackend.h
    src/stt/WhisperCppBackend.cpp
    src/paste/Paster.h
    src/paste/Paster.cpp
    src/llm/LlmBeautifier.h
    src/llm/LlmBeautifier.cpp
    src/update/Updater.h
    src/update/Updater.cpp
)

target_include_directories(scrybe PRIVATE src)
target_compile_definitions(scrybe PRIVATE SCRYBE_VERSION="${PROJECT_VERSION}")

# OpenVINO backend is compiled in only when available on this machine.
if(SCRYBE_OPENVINO_ENABLED)
    target_sources(scrybe PRIVATE
        src/stt/OpenVinoBackend.h
        src/stt/OpenVinoBackend.cpp)
    target_include_directories(scrybe PRIVATE ${SCRYBE_GENAI_INC})
    target_compile_definitions(scrybe PRIVATE SCRYBE_HAVE_OPENVINO=1)
    target_link_libraries(scrybe PRIVATE openvino::runtime ${SCRYBE_GENAI_LIB})
else()
    target_compile_definitions(scrybe PRIVATE SCRYBE_HAVE_OPENVINO=0)
endif()

qt_add_qml_module(scrybe
    URI Scrybe
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Island.qml
        qml/Visualizer.qml
        qml/TranscriptBox.qml
        qml/SettingsWindow.qml
)

target_link_libraries(scrybe PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Widgets Qt6::Network
    Qt6::Multimedia
    LayerShellQt::Interface
    KF6::GlobalAccel)

if(SCRYBE_OPENVINO_ENABLED)
    message(STATUS "scrybe: OpenVINO ${OpenVINO_VERSION} + GenAI (${SCRYBE_GENAI_LIB})")
else()
    message(STATUS "scrybe: OpenVINO backend DISABLED "
                   "(non-Intel machine or GenAI C++ not installed) — "
                   "using faster-whisper / whisper.cpp")
endif()
