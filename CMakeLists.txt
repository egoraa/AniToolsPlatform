cmake_minimum_required(VERSION 4.1)
project(AniToolsPlatform)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Заголовки платформы — единая точка подключения для приложения и тестов.
add_library(atp_platform INTERFACE)
target_include_directories(atp_platform INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Заголовки собираются глобом — только для видимости в IDE; CONFIGURE_DEPENDS
# перезапускает конфигурацию при появлении новых файлов.
file(GLOB_RECURSE ATP_PLATFORM_HEADERS CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/*.hpp)

add_executable(AniToolsPlatform src/main.cpp ${ATP_PLATFORM_HEADERS})
target_link_libraries(AniToolsPlatform PRIVATE atp_platform)

enable_testing()
add_subdirectory(tests)
