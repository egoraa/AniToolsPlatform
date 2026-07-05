cmake_minimum_required(VERSION 4.1)
project(AniToolsPlatform)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Заголовки платформы — единая точка подключения для приложения и тестов.
add_library(atp_platform INTERFACE)
target_include_directories(atp_platform INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(AniToolsPlatform src/main.cpp
        src/platform/module.hpp
        src/platform/io.hpp)
target_link_libraries(AniToolsPlatform PRIVATE atp_platform)

enable_testing()
add_subdirectory(tests)
