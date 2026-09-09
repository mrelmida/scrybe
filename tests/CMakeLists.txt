find_package(Qt6 REQUIRED COMPONENTS Test)

foreach(t version resample wav text vad)
    add_executable(tst_${t} tst_${t}.cpp)
    target_link_libraries(tst_${t} PRIVATE scrybe_core Qt6::Test)
    add_test(NAME ${t} COMMAND tst_${t})
endforeach()

# Sidecar protocol test (plain Python; exits 77 = SKIP when numpy is missing).
find_program(SCRYBE_PYTHON3 python3 REQUIRED)
if(SCRYBE_PYTHON3)
    add_test(NAME sidecar_protocol
        COMMAND ${SCRYBE_PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/test_sidecar.py
                ${CMAKE_SOURCE_DIR}/scripts/faster_whisper_sidecar.py)
    set_tests_properties(sidecar_protocol PROPERTIES SKIP_RETURN_CODE 77)
endif()

# Async recording regressions remain runnable without GUI/KDE or actual models.
add_executable(tst_stt_session tst_stt_session.cpp
    ${CMAKE_SOURCE_DIR}/src/stt/SttEngine.cpp)
target_link_libraries(tst_stt_session PRIVATE scrybe_core Qt6::Test)
add_test(NAME stt_session COMMAND tst_stt_session)

find_package(Qt6 REQUIRED COMPONENTS Network)
add_executable(tst_llm_session tst_llm_session.cpp
    ${CMAKE_SOURCE_DIR}/src/llm/LlmBeautifier.cpp)
target_link_libraries(tst_llm_session PRIVATE scrybe_core Qt6::Test Qt6::Network)
add_test(NAME llm_session COMMAND tst_llm_session)

if(SCRYBE_BUILD_APP)
    find_package(Qt6 REQUIRED COMPONENTS Gui Multimedia)
    add_executable(tst_controller_session tst_controller_session.cpp
        ${CMAKE_SOURCE_DIR}/src/core/Controller.cpp
        ${CMAKE_SOURCE_DIR}/src/core/ModelDownloader.cpp
        ${CMAKE_SOURCE_DIR}/src/audio/AudioCapture.cpp
        ${CMAKE_SOURCE_DIR}/src/stt/SttEngine.cpp
        ${CMAKE_SOURCE_DIR}/src/llm/LlmBeautifier.cpp
        ${CMAKE_SOURCE_DIR}/src/paste/Paster.cpp
        ${CMAKE_SOURCE_DIR}/src/update/Updater.cpp)
    target_compile_definitions(tst_controller_session PRIVATE SCRYBE_VERSION="${PROJECT_VERSION}")
    target_link_libraries(tst_controller_session PRIVATE scrybe_core Qt6::Test
        Qt6::Network Qt6::Gui Qt6::Multimedia)
    add_test(NAME controller_session COMMAND tst_controller_session)
endif()

add_executable(tst_model_downloader tst_model_downloader.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ModelDownloader.cpp)
target_link_libraries(tst_model_downloader PRIVATE scrybe_core Qt6::Test)
add_test(NAME model_downloader COMMAND tst_model_downloader)

if(SCRYBE_BUILD_APP)
    add_executable(tst_audio_capture tst_audio_capture.cpp
        ${CMAKE_SOURCE_DIR}/src/audio/AudioCapture.cpp)
    target_link_libraries(tst_audio_capture PRIVATE scrybe_core Qt6::Test Qt6::Multimedia)
    add_test(NAME audio_capture COMMAND tst_audio_capture)
endif()

if(SCRYBE_PYTHON3)
    add_executable(tst_faster_whisper_recovery tst_faster_whisper_recovery.cpp
        ${CMAKE_SOURCE_DIR}/src/stt/FasterWhisperBackend.cpp)
    target_compile_definitions(tst_faster_whisper_recovery PRIVATE
        SCRYBE_TEST_PYTHON="${SCRYBE_PYTHON3}")
    target_link_libraries(tst_faster_whisper_recovery PRIVATE scrybe_core Qt6::Test)
    add_test(NAME faster_whisper_recovery COMMAND tst_faster_whisper_recovery)
endif()
