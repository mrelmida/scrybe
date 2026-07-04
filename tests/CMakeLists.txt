find_package(Qt6 REQUIRED COMPONENTS Test)

foreach(t version resample wav text)
    add_executable(tst_${t} tst_${t}.cpp)
    target_link_libraries(tst_${t} PRIVATE scrybe_core Qt6::Test)
    add_test(NAME ${t} COMMAND tst_${t})
endforeach()

# Sidecar protocol test (plain Python; exits 77 = SKIP when numpy is missing).
find_program(SCRYBE_PYTHON3 python3)
if(SCRYBE_PYTHON3)
    add_test(NAME sidecar_protocol
        COMMAND ${SCRYBE_PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/test_sidecar.py
                ${CMAKE_SOURCE_DIR}/scripts/faster_whisper_sidecar.py)
    set_tests_properties(sidecar_protocol PROPERTIES SKIP_RETURN_CODE 77)
endif()
