# Логика studio — header-only библиотека: тесты подключают её напрямую.
add_library(atp_studio_lib INTERFACE)
target_include_directories(atp_studio_lib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(atp_studio_lib INTERFACE atp_runtime)

# Заголовки — глобом, только для видимости в IDE (как в корневом CMakeLists).
file(GLOB_RECURSE ATP_STUDIO_HEADERS CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/include/atp/studio/*.hpp)

# --- GUI: собирается только при найденном Qt6 — конфигурация без Qt
# (например, Linux до установки qt6-base-dev) остаётся рабочей.
# Модуль ищет Qt, а при ATP_AUTO_INSTALL_QT — доустанавливает его через
# aqtinstall; после include() определён Qt6_FOUND.
include(${CMAKE_SOURCE_DIR}/cmake/AutoInstallQt.cmake)
if (Qt6_FOUND)
    add_subdirectory(ui)
else ()
    message(STATUS "Qt6 not found — atp_studio is skipped")
endif ()
