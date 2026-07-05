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

add_executable(atp_tests
        io_tests.cpp
        module_tests.cpp)
target_link_libraries(atp_tests PRIVATE atp_platform GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(atp_tests)
