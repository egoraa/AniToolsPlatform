cmake_minimum_required(VERSION 4.1)
project(AniToolsPlatform)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Если Qt не найден, скачать его через aqtinstall в external/ (см. cmake/AutoInstallQt.cmake).
# По умолчанию OFF: конфигурации с системным Qt или Qt из пресета не меняются.
option(ATP_AUTO_INSTALL_QT "Download Qt via aqtinstall into external/ if not found" OFF)

# Заголовки платформы — единая точка подключения для приложения и тестов.
add_library(atp_platform INTERFACE)
target_include_directories(atp_platform INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Заголовки собираются глобом — только для видимости в IDE; CONFIGURE_DEPENDS
# перезапускает конфигурацию при появлении новых файлов.
file(GLOB_RECURSE ATP_PLATFORM_HEADERS CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/include/atp/*.hpp)

# Хост-рантайм: исполнение пайплайна, загрузка плагинов и конфига. Отдельная
# цель, чтобы SDK автора модулей (atp_platform) не тянул хост-машинерию:
# плагины видят только include/, хост-приложения — include/ + src/runtime.
add_subdirectory(src/runtime)

# GLOB здесь, а не в src/runtime: переменную потребляет examples/demo,
# а переменные подкаталога до соседних каталогов не доходят.
file(GLOB_RECURSE ATP_RUNTIME_HEADERS CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/include/atp/*.hpp)

add_subdirectory(examples/demo)
add_subdirectory(examples/plugin_demo)
add_subdirectory(src/app)
add_subdirectory(src/studio)

enable_testing()
add_subdirectory(tests)
