include(FetchContent)

set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
# MSVC: gtest has to use the same (dynamic) CRT as the project.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# The dependency archive and sources are downloaded into the project directory, while googletest's
# build artefacts stay in the profile's build directory and are not shared between profiles.
FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
        DOWNLOAD_DIR ${CMAKE_SOURCE_DIR}/external/downloads
        SOURCE_DIR ${CMAKE_SOURCE_DIR}/external/googletest-src
)
FetchContent_MakeAvailable(googletest)

add_subdirectory(test_plugin)

add_executable(atp_tests
        enum_names_tests.cpp
        group_tests.cpp
        io_tests.cpp
        mcp_arguments_tests.cpp
        mcp_catalog_tools_tests.cpp
        mcp_document_tools_tests.cpp
        mcp_execution_tools_tests.cpp
        mcp_json_rpc_tests.cpp
        mcp_options_tests.cpp
        mcp_registry_tests.cpp
        mcp_server_tests.cpp
        mcp_settings_tools_tests.cpp
        mcp_workspace_tests.cpp
        module_tests.cpp
        module_factory_tests.cpp
        module_loader_tests.cpp
        module_registry_tests.cpp
        pipeline_runner_tests.cpp
        pipeline_tests.cpp
        ports_tests.cpp
        property_codec_tests.cpp
        property_tests.cpp
        runtime_config_loader_tests.cpp
        runtime_config_model_tests.cpp
        runtime_config_validator_tests.cpp
        runtime_pipeline_builder_tests.cpp
        runtime_property_override_tests.cpp
        service_directory_tests.cpp
        studio_add_group_tests.cpp
        studio_add_module_tests.cpp
        studio_document_tests.cpp
        studio_layout_tests.cpp
        studio_module_manager_tests.cpp
        studio_port_types_tests.cpp
        studio_property_sync_tests.cpp
        studio_session_tests.cpp
        studio_settings_tests.cpp
        studio_thread_resolve_tests.cpp
        version_tests.cpp)
# CMAKE_DL_LIBS: on Linux dlopen may require -ldl, on Windows it is empty.
target_link_libraries(atp_tests PRIVATE atp_runtime atp_studio_lib atp_mcp_lib GTest::gtest_main ${CMAKE_DL_LIBS})

# The test plugin paths go to the tests' compiler; $<PATH:CMAKE_PATH,...> normalises the separators,
# since Windows backslashes would break the string literal.
add_dependencies(atp_tests test_plugin test_plugin_bad_abi test_plugin_empty)
target_compile_definitions(atp_tests PRIVATE
        ATP_TEST_PLUGIN="$<PATH:CMAKE_PATH,$<TARGET_FILE:test_plugin>>"
        ATP_TEST_PLUGIN_BAD_ABI="$<PATH:CMAKE_PATH,$<TARGET_FILE:test_plugin_bad_abi>>"
        ATP_TEST_PLUGIN_EMPTY="$<PATH:CMAKE_PATH,$<TARGET_FILE:test_plugin_empty>>")

include(GoogleTest)
gtest_discover_tests(atp_tests)
