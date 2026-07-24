include(FetchContent)

set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
# MSVC: gtest должен использовать тот же (динамический) CRT, что и проект.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# Архив и исходники зависимостей скачиваются в папку проекта; build-артефакты
# googletest остаются в build-каталоге профиля и между профилями не делятся.
FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
        DOWNLOAD_DIR ${CMAKE_SOURCE_DIR}/external/downloads
        SOURCE_DIR ${CMAKE_SOURCE_DIR}/external/googletest-src
)
FetchContent_MakeAvailable(googletest)

add_subdirectory(test_plugin)

add_executable(atp_tests
        group_tests.cpp
        io_tests.cpp
        module_tests.cpp
        module_factory_tests.cpp
        module_loader_tests.cpp
        module_registry_tests.cpp
        pipeline_runner_tests.cpp
        pipeline_tests.cpp
        ports_tests.cpp
        runtime_config_loader_tests.cpp
        runtime_config_model_tests.cpp
        runtime_config_validator_tests.cpp
        runtime_pipeline_builder_tests.cpp
        service_directory_tests.cpp
        studio_add_module_tests.cpp
        studio_document_tests.cpp
        studio_layout_tests.cpp
        studio_module_manager_tests.cpp
        studio_port_types_tests.cpp
        studio_session_tests.cpp
        studio_settings_tests.cpp
        version_tests.cpp)
# CMAKE_DL_LIBS: на Linux dlopen может требовать -ldl, на Windows пусто.
target_link_libraries(atp_tests PRIVATE atp_runtime atp_studio_lib GTest::gtest_main ${CMAKE_DL_LIBS})

# Пути к тестовым плагинам — компилятору тестов; $<PATH:CMAKE_PATH,...>
# нормализует разделители (обратные слэши Windows сломали бы строковый литерал).
add_dependencies(atp_tests test_plugin test_plugin_bad_abi test_plugin_empty)
target_compile_definitions(atp_tests PRIVATE
        ATP_TEST_PLUGIN="$<PATH:CMAKE_PATH,$<TARGET_FILE:test_plugin>>"
        ATP_TEST_PLUGIN_BAD_ABI="$<PATH:CMAKE_PATH,$<TARGET_FILE:test_plugin_bad_abi>>"
        ATP_TEST_PLUGIN_EMPTY="$<PATH:CMAKE_PATH,$<TARGET_FILE:test_plugin_empty>>")

include(GoogleTest)
gtest_discover_tests(atp_tests)
