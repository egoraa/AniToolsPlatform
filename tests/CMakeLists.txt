include(FetchContent)

set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
        DOWNLOAD_DIR ${CMAKE_SOURCE_DIR}/external/downloads
        SOURCE_DIR ${CMAKE_SOURCE_DIR}/external/googletest-src
        SYSTEM
)
FetchContent_MakeAvailable(googletest)

add_subdirectory(test_plugin)

set(ATP_TESTS_DIRS platform runtime mcp studio support)
foreach (dir IN LISTS ATP_TESTS_DIRS)
    file(GLOB_RECURSE dir_sources CONFIGURE_DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/${dir}/*.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/${dir}/*.hpp)
    list(APPEND ATP_TESTS_SOURCES ${dir_sources})
endforeach ()

add_executable(atp_tests ${ATP_TESTS_SOURCES})
target_include_directories(atp_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(atp_tests PRIVATE atp_runtime atp_studio_lib atp_mcp_lib atp_warnings GTest::gtest_main ${CMAKE_DL_LIBS})

add_dependencies(atp_tests test_plugin test_plugin_ports test_plugin_bad_abi test_plugin_empty)
target_compile_definitions(atp_tests PRIVATE
        ATP_TEST_PLUGIN="$<PATH:CMAKE_PATH,$<TARGET_FILE:test_plugin>>"
        ATP_TEST_PLUGIN_PORTS="$<PATH:CMAKE_PATH,$<TARGET_FILE:test_plugin_ports>>"
        ATP_TEST_PLUGIN_BAD_ABI="$<PATH:CMAKE_PATH,$<TARGET_FILE:test_plugin_bad_abi>>"
        ATP_TEST_PLUGIN_EMPTY="$<PATH:CMAKE_PATH,$<TARGET_FILE:test_plugin_empty>>")

if (ATP_ASAN_RUNTIME)
    add_custom_command(TARGET atp_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${ATP_ASAN_RUNTIME}" "$<TARGET_FILE_DIR:atp_tests>"
            COMMENT "AddressSanitizer runtime next to atp_tests")
endif ()

include(GoogleTest)
gtest_discover_tests(atp_tests)

if (TARGET atp_studio_ui)
    file(GLOB_RECURSE ATP_UI_TESTS_SOURCES CONFIGURE_DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/ui/*.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ui/*.hpp)

    add_executable(atp_ui_tests ${ATP_UI_TESTS_SOURCES})
    target_include_directories(atp_ui_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    # atp_mcp_lib is here for the attach tests alone: they raise a real control channel in the test
    # process, because attaching to a mock would prove nothing about the protocol.
    target_link_libraries(atp_ui_tests PRIVATE atp_studio_ui atp_mcp_lib atp_warnings GTest::gtest_main)

    # The suite forces the offscreen platform (tests/ui/qt_app.hpp) so it runs with no display;
    # windeployqt would only bring qwindows, which is not the one being asked for.
    atp_deploy_qt(atp_ui_tests Qt6::QOffscreenIntegrationPlugin)

    if (ATP_ASAN_RUNTIME)
        add_custom_command(TARGET atp_ui_tests POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${ATP_ASAN_RUNTIME}" "$<TARGET_FILE_DIR:atp_ui_tests>"
                COMMENT "AddressSanitizer runtime next to atp_ui_tests")
    endif ()

    # Labelled, so a run can ask for the widget tests alone. The CI job that has Qt builds this target
    # and nothing else — the headless suite is already covered by four other jobs — and `ctest -L ui`
    # is what keeps it from also picking up the atp_tests_NOT_BUILT entry that gtest_discover_tests
    # leaves behind for a target nobody built.
    gtest_discover_tests(atp_ui_tests PROPERTIES LABELS ui)
endif ()

# Benchmarks sit beside the tests because they ask the same kind of question about the same code —
# does it still do what it should, and now also at what cost. They are a separate target and a
# separate opt-in switch: ctest never runs them, and they are not part of ATP_TESTS_DIRS, so nothing
# here sweeps their sources into atp_tests.
if (ATP_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif ()
