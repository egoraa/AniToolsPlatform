include(FetchContent)

set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
        DOWNLOAD_DIR ${CMAKE_SOURCE_DIR}/external/downloads
        SOURCE_DIR ${CMAKE_SOURCE_DIR}/external/googletest-src
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
target_link_libraries(atp_tests PRIVATE atp_runtime atp_studio_lib atp_mcp_lib GTest::gtest_main ${CMAKE_DL_LIBS})

add_dependencies(atp_tests test_plugin test_plugin_ports test_plugin_bad_abi test_plugin_empty)
target_compile_definitions(atp_tests PRIVATE
        ATP_TEST_PLUGIN="$<PATH:CMAKE_PATH,$<TARGET_FILE:test_plugin>>"
        ATP_TEST_PLUGIN_PORTS="$<PATH:CMAKE_PATH,$<TARGET_FILE:test_plugin_ports>>"
        ATP_TEST_PLUGIN_BAD_ABI="$<PATH:CMAKE_PATH,$<TARGET_FILE:test_plugin_bad_abi>>"
        ATP_TEST_PLUGIN_EMPTY="$<PATH:CMAKE_PATH,$<TARGET_FILE:test_plugin_empty>>")

include(GoogleTest)
gtest_discover_tests(atp_tests)

if (TARGET atp_studio_ui)
    file(GLOB_RECURSE ATP_UI_TESTS_SOURCES CONFIGURE_DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/ui/*.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ui/*.hpp)

    add_executable(atp_ui_tests ${ATP_UI_TESTS_SOURCES})
    target_include_directories(atp_ui_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(atp_ui_tests PRIVATE atp_studio_ui GTest::gtest_main)

    if (WIN32)
        add_custom_command(TARGET atp_ui_tests POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "$<TARGET_RUNTIME_DLLS:atp_ui_tests>" "$<TARGET_FILE_DIR:atp_ui_tests>"
                COMMAND_EXPAND_LISTS
                COMMENT "Qt runtime next to atp_ui_tests")
    endif ()

    gtest_discover_tests(atp_ui_tests)
endif ()
