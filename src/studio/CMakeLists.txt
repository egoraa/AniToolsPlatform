# Логика studio — header-only библиотека: тесты подключают её напрямую,
# GUI-исполняемый файл придёт фазой 3.
add_library(atp_studio_lib INTERFACE)
target_include_directories(atp_studio_lib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(atp_studio_lib INTERFACE atp_runtime)

file(GLOB_RECURSE ATP_STUDIO_HEADERS CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/atp/studio/*.hpp)

# --- GUI на Qt: собирается только при найденном Qt6 — конфигурация без
# Qt (например, Linux до установки qt6-base-dev) остаётся рабочей.
# Модуль ищет Qt, а при ATP_AUTO_INSTALL_QT — доустанавливает его через aqtinstall;
# после include() определён Qt6_FOUND.
include(${CMAKE_SOURCE_DIR}/cmake/AutoInstallQt.cmake)
if (Qt6_FOUND)
    qt_standard_project_setup()
    qt_add_executable(atp_studio_qt qt/main.cpp)
    target_include_directories(atp_studio_qt PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/qt)
    target_link_libraries(atp_studio_qt PRIVATE atp_studio_lib Qt6::Widgets ${CMAKE_DL_LIBS})
    # заголовки — в целях видимости IDE, как GLOB'ы остального проекта
    file(GLOB_RECURSE ATP_STUDIO_QT_HEADERS CONFIGURE_DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/qt/atp/studio/qt/*.hpp)
    target_sources(atp_studio_qt PRIVATE ${ATP_STUDIO_QT_HEADERS})

    # windeployqt кладёт Qt-DLL рядом с бинарём: запуск из проводника/IDE
    # работает без ручного PATH на каталог Qt.
    if (WIN32)
        find_program(ATP_WINDEPLOYQT windeployqt HINTS "${Qt6_DIR}/../../../bin")
        if (ATP_WINDEPLOYQT)
            add_custom_command(TARGET atp_studio_qt POST_BUILD
                    COMMAND "${ATP_WINDEPLOYQT}" --no-translations --no-compiler-runtime
                            "$<TARGET_FILE:atp_studio_qt>"
                    COMMENT "windeployqt: Qt runtime next to atp_studio_qt")
        endif ()
    endif ()
else ()
    message(STATUS "Qt6 not found — atp_studio_qt is skipped")
endif ()
