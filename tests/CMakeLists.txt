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
# tests/bridges is deliberately not in that list, and that is the one directory here whose files are
# named rather than globbed: each of its suites drives one real library through module_loader and
# names it through a definition only that bridge's target provides, so a build with one bridge and not
# the other would compile a file whose ATP_*_BRIDGE is undeclared. Each is added below, beside the
# definition it needs.
foreach (dir IN LISTS ATP_TESTS_DIRS)
    file(GLOB_RECURSE dir_sources CONFIGURE_DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/${dir}/*.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/${dir}/*.hpp)
    list(APPEND ATP_TESTS_SOURCES ${dir_sources})
endforeach ()

add_executable(atp_tests ${ATP_TESTS_SOURCES})
target_include_directories(atp_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(atp_tests PRIVATE atp_runtime atp_studio_lib atp_mcp_lib atp_warnings GTest::gtest_main ${CMAKE_DL_LIBS})

# One translation unit per SDK and runtime header, each including exactly that header and nothing
# else. A header that compiles only because something else was included before it is a latent break
# for whoever includes it first, and the umbrella headers hide the problem by including everything in
# one go. Generated from the same globs the IDE tree uses, so a header added later is covered without
# an edit. The two include roots are not interchangeable: `rel` becomes both the text of the #include
# and the name of the generated file, so measuring a runtime header from the source root would emit
# `#include <src/runtime/include/atp/...>`, which no include directory resolves.
set(ATP_HEADER_TU_DIR ${CMAKE_CURRENT_BINARY_DIR}/header_units)
file(MAKE_DIRECTORY ${ATP_HEADER_TU_DIR})
set(ATP_HEADER_TUS "")
foreach (header IN LISTS ATP_PLATFORM_HEADERS ATP_RUNTIME_HEADERS)
    if (header MATCHES "/src/runtime/include/")
        file(RELATIVE_PATH rel ${CMAKE_SOURCE_DIR}/src/runtime/include ${header})
    else ()
        file(RELATIVE_PATH rel ${CMAKE_SOURCE_DIR}/include ${header})
    endif ()
    string(REGEX REPLACE "[/.]" "_" slug ${rel})
    if (rel MATCHES "\\.h$")
        set(tu ${ATP_HEADER_TU_DIR}/${slug}.c)
    else ()
        set(tu ${ATP_HEADER_TU_DIR}/${slug}.cpp)
    endif ()
    file(GENERATE OUTPUT ${tu} CONTENT "#include <${rel}>\n")
    list(APPEND ATP_HEADER_TUS ${tu})
endforeach ()
target_sources(atp_tests PRIVATE ${ATP_HEADER_TUS})

add_dependencies(atp_tests test_plugin test_plugin_ports test_plugin_bad_abi test_plugin_empty
        test_plugin_c test_plugin_c_bad_abi test_plugin_c_bad_desc)
target_compile_definitions(atp_tests PRIVATE
        ATP_TEST_PLUGIN="$<PATH:CMAKE_PATH,$<TARGET_FILE:test_plugin>>"
        ATP_TEST_PLUGIN_PORTS="$<PATH:CMAKE_PATH,$<TARGET_FILE:test_plugin_ports>>"
        ATP_TEST_PLUGIN_BAD_ABI="$<PATH:CMAKE_PATH,$<TARGET_FILE:test_plugin_bad_abi>>"
        ATP_TEST_PLUGIN_EMPTY="$<PATH:CMAKE_PATH,$<TARGET_FILE:test_plugin_empty>>"
        ATP_TEST_PLUGIN_C="$<PATH:CMAKE_PATH,$<TARGET_FILE:test_plugin_c>>"
        ATP_TEST_PLUGIN_C_BAD_ABI="$<PATH:CMAKE_PATH,$<TARGET_FILE:test_plugin_c_bad_abi>>"
        ATP_TEST_PLUGIN_C_BAD_DESC="$<PATH:CMAKE_PATH,$<TARGET_FILE:test_plugin_c_bad_desc>>")

if (TARGET atp_python_bridge)
    add_dependencies(atp_tests atp_python_bridge)
    target_sources(atp_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/bridges/python_bridge_tests.cpp)
    target_compile_definitions(atp_tests PRIVATE
            ATP_PYTHON_BRIDGE="$<PATH:CMAKE_PATH,$<TARGET_FILE:atp_python_bridge>>"
            ATP_PYTHON_BRIDGE_SCRIPTS="$<PATH:CMAKE_PATH,${CMAKE_CURRENT_SOURCE_DIR}/bridges/scripts>"
            ATP_PYTHON_BRIDGE_SCRIPTS_BROKEN="$<PATH:CMAKE_PATH,${CMAKE_CURRENT_SOURCE_DIR}/bridges/scripts_broken>")
endif ()

if (TARGET atp_lua_bridge)
    add_dependencies(atp_tests atp_lua_bridge)
    target_sources(atp_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/bridges/lua_bridge_tests.cpp)
    target_compile_definitions(atp_tests PRIVATE
            ATP_LUA_BRIDGE="$<PATH:CMAKE_PATH,$<TARGET_FILE:atp_lua_bridge>>"
            ATP_LUA_BRIDGE_SCRIPTS="$<PATH:CMAKE_PATH,${CMAKE_CURRENT_SOURCE_DIR}/bridges/lua_scripts>"
            ATP_LUA_BRIDGE_SCRIPTS_BROKEN="$<PATH:CMAKE_PATH,${CMAKE_CURRENT_SOURCE_DIR}/bridges/lua_scripts_broken>")
endif ()

if (ATP_ASAN_RUNTIME)
    # One staging target rather than a POST_BUILD step per suite: both test executables live in bin/
    # now, so two commands would write the same file to the same path, concurrently under a parallel
    # generator. atp_tests is the anchor because it is always defined and the directory is its own.
    add_custom_target(atp_asan_runtime ALL
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${ATP_ASAN_RUNTIME}" "$<TARGET_FILE_DIR:atp_tests>"
            COMMENT "AddressSanitizer runtime beside the test executables")
    add_dependencies(atp_asan_runtime atp_tests)
endif ()

include(GoogleTest)
gtest_discover_tests(atp_tests)

if (TARGET atp_studio_ui)
    file(GLOB_RECURSE ATP_UI_TESTS_SOURCES CONFIGURE_DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/ui/*.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ui/*.hpp)

    add_executable(atp_ui_tests ${ATP_UI_TESTS_SOURCES})
    target_include_directories(atp_ui_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    # The C fixture plugin, so a widget test can ask a real manager what a real load produced —
    # its two modules differ in exactly what the plugins dock branches on, a declared source file
    # and none.
    add_dependencies(atp_ui_tests test_plugin_c)
    target_compile_definitions(atp_ui_tests PRIVATE
            ATP_TEST_PLUGIN_C="$<PATH:CMAKE_PATH,$<TARGET_FILE:test_plugin_c>>")
    # atp_mcp_lib is here for the attach tests alone: they raise a real control channel in the test
    # process, because attaching to a mock would prove nothing about the protocol.
    target_link_libraries(atp_ui_tests PRIVATE atp_studio_ui atp_mcp_lib atp_warnings GTest::gtest_main)

    # The suite forces the offscreen platform (tests/ui/qt_app.hpp) so it runs with no display;
    # windeployqt would only bring qwindows, which is not the one being asked for.
    atp_deploy_qt(atp_ui_tests Qt6::QOffscreenIntegrationPlugin)

    # atp_asan_runtime stages the runtime into bin/, which is where this suite runs from as well. A
    # TARGET test rather than a repeat of the ATP_ASAN_RUNTIME condition, so the two cannot drift
    # apart; it is false on every platform where that runtime is not a thing to copy.
    if (TARGET atp_asan_runtime)
        add_dependencies(atp_ui_tests atp_asan_runtime)
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
