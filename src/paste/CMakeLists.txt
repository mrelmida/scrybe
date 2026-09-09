find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
set(protocol_sources)
foreach(protocol ext-data-control-v1 wlr-data-control-unstable-v1)
    set(xml ${CMAKE_CURRENT_SOURCE_DIR}/protocols/${protocol}.xml)
    set(header ${CMAKE_CURRENT_BINARY_DIR}/${protocol}-client.h)
    set(code ${CMAKE_CURRENT_BINARY_DIR}/${protocol}-protocol.c)
    add_custom_command(OUTPUT ${header}
        COMMAND ${WAYLAND_SCANNER} client-header ${xml} ${header}
        DEPENDS ${xml} VERBATIM)
    add_custom_command(OUTPUT ${code}
        COMMAND ${WAYLAND_SCANNER} private-code ${xml} ${code}
        DEPENDS ${xml} VERBATIM)
    list(APPEND protocol_sources ${header} ${code})
endforeach()
add_executable(scrybe-clipboard ClipboardHelper.cpp ${protocol_sources})
target_include_directories(scrybe-clipboard PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(scrybe-clipboard PRIVATE Qt6::Core PkgConfig::WAYLAND_CLIENT)

if(BUILD_TESTING AND TARGET Qt6::Test)
    pkg_check_modules(WAYLAND_SERVER QUIET IMPORTED_TARGET wayland-server)
    if(WAYLAND_SERVER_FOUND)
        foreach(protocol ext-data-control-v1 wlr-data-control-unstable-v1)
            set(server_header ${CMAKE_CURRENT_BINARY_DIR}/${protocol}-server.h)
            add_custom_command(OUTPUT ${server_header}
                COMMAND ${WAYLAND_SCANNER} server-header
                    ${CMAKE_CURRENT_SOURCE_DIR}/protocols/${protocol}.xml ${server_header}
                DEPENDS protocols/${protocol}.xml VERBATIM)
            set(test_target tst_clipboard_helper)
            if(protocol STREQUAL "wlr-data-control-unstable-v1")
                set(test_target tst_clipboard_helper_legacy)
            endif()
            add_executable(${test_target} ../../tests/tst_clipboard_helper.cpp
                ${server_header} ${CMAKE_CURRENT_BINARY_DIR}/${protocol}-protocol.c)
            target_include_directories(${test_target} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
            target_link_libraries(${test_target} PRIVATE Qt6::Core Qt6::Test PkgConfig::WAYLAND_SERVER)
            target_compile_definitions(${test_target} PRIVATE CLIPBOARD_HELPER="$<TARGET_FILE:scrybe-clipboard>")
            if(protocol STREQUAL "wlr-data-control-unstable-v1")
                target_compile_definitions(${test_target} PRIVATE TEST_LEGACY=1)
            endif()
            add_dependencies(${test_target} scrybe-clipboard)
            add_test(NAME ${test_target} COMMAND ${test_target})
        endforeach()
    endif()
endif()
