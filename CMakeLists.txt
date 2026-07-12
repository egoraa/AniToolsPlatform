cmake_minimum_required(VERSION 4.1)
project(AniToolsPlatform)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Заголовки платформы — единая точка подключения для приложения и тестов.
add_library(atp_platform INTERFACE)
target_include_directories(atp_platform INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Заголовки собираются глобом — только для видимости в IDE; CONFIGURE_DEPENDS
# перезапускает конфигурацию при появлении новых файлов.
file(GLOB_RECURSE ATP_PLATFORM_HEADERS CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/include/atp/*.hpp)

# Хост-платформа: исполнение пайплайна и загрузка плагинов. Отдельная цель,
# чтобы SDK автора модулей (atp_platform) не тянул хост-машинерию: плагины
# видят только include/, хост — include/ + src/.
add_library(atp_host INTERFACE)
target_include_directories(atp_host INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(atp_host INTERFACE atp_platform)

file(GLOB_RECURSE ATP_HOST_HEADERS CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/atp/*.hpp)

add_subdirectory(examples/demo)
add_subdirectory(examples/plugin_demo)

enable_testing()
add_subdirectory(tests)
