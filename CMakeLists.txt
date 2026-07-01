cmake_minimum_required(VERSION 3.21)
project(scrybe VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick Widgets Network Multimedia)

# M2: overlay anchoring (KWin/Wayland) + global hotkey registration.
find_package(LayerShellQt REQUIRED)
find_package(KF6GlobalAccel REQUIRED)

# M3: OpenVINO Whisper STT.
# Core OpenVINO comes from the wheel-extracted tree (headers + cmake + libs);
# GenAI ships only the runtime .so there, so we add its headers/lib manually.
set(SCRYBE_OV_ROOT "/opt/scrybe-openvino" CACHE PATH "OpenVINO install root")
if(NOT OpenVINO_DIR)
    set(OpenVINO_DIR "${SCRYBE_OV_ROOT}/openvino/cmake")
endif()
find_package(OpenVINO REQUIRED COMPONENTS Runtime)
set(SCRYBE_GENAI_INC "${SCRYBE_OV_ROOT}/openvino_genai/include")
find_library(SCRYBE_GENAI_LIB NAMES openvino_genai
    PATHS "${SCRYBE_OV_ROOT}/openvino_genai" NO_DEFAULT_PATH)
if(NOT SCRYBE_GENAI_LIB OR NOT EXISTS "${SCRYBE_GENAI_INC}/openvino/genai/whisper_pipeline.hpp")
    message(FATAL_ERROR
        "OpenVINO GenAI C++ (headers/lib) not found under ${SCRYBE_OV_ROOT}.\n"
        "Run ./build-and-setup.sh to install it.")
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
    src/stt/OpenVinoBackend.h
    src/stt/OpenVinoBackend.cpp
    src/stt/FasterWhisperBackend.h
    src/stt/FasterWhisperBackend.cpp
    src/stt/WhisperCppBackend.h
    src/stt/WhisperCppBackend.cpp
    src/paste/Paster.h
    src/paste/Paster.cpp
    src/llm/LlmBeautifier.h
    src/llm/LlmBeautifier.cpp
)

target_include_directories(scrybe PRIVATE src ${SCRYBE_GENAI_INC})
target_compile_definitions(scrybe PRIVATE SCRYBE_VERSION="${PROJECT_VERSION}")

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
    KF6::GlobalAccel
    openvino::runtime
    ${SCRYBE_GENAI_LIB})

message(STATUS "scrybe: OpenVINO ${OpenVINO_VERSION} + GenAI (${SCRYBE_GENAI_LIB})")
