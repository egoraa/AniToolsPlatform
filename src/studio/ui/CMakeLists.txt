qt_standard_project_setup()

# Исходники — глобом: новые пары hpp/cpp подхватываются без правки CMake;
# заголовки в списке — видимость в IDE. Приватные заголовки лежат рядом с
# исходниками и включаются кавычками — include-dir не нужен.
file(GLOB ATP_STUDIO_UI_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp)
qt_add_executable(atp_studio ${ATP_STUDIO_UI_SOURCES})
target_link_libraries(atp_studio PRIVATE atp_studio_lib Qt6::Widgets ${CMAKE_DL_LIBS})

# windeployqt кладёт Qt-DLL рядом с бинарём: запуск из проводника/IDE
# работает без ручного PATH на каталог Qt.
if (WIN32)
    find_program(ATP_WINDEPLOYQT windeployqt HINTS "${Qt6_DIR}/../../../bin")
    if (ATP_WINDEPLOYQT)
        add_custom_command(TARGET atp_studio POST_BUILD
                COMMAND "${ATP_WINDEPLOYQT}" --no-translations --no-compiler-runtime
                        "$<TARGET_FILE:atp_studio>"
                COMMENT "windeployqt: Qt runtime next to atp_studio")
    endif ()
endif ()
