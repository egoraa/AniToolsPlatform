qt_standard_project_setup()

# The sources are globbed, so new hpp/cpp pairs are picked up without touching CMake; the headers
# are listed for IDE visibility. Private headers sit next to the sources and are included with
# quotes, so no include directory is needed.
file(GLOB ATP_STUDIO_UI_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp)
qt_add_executable(atp_studio ${ATP_STUDIO_UI_SOURCES})
target_link_libraries(atp_studio PRIVATE atp_studio_lib Qt6::Widgets ${CMAKE_DL_LIBS})

# The application mark. One .ico serves both users of it: the resource gives the windows their icon
# on every platform, and on Windows the resource script hands the same file to the linker, so the
# executable wears it in Explorer and on the taskbar too. The alias keeps the resource path short —
# :/atp.ico rather than the directory this file sits in.
set_source_files_properties(resources/atp.ico PROPERTIES QT_RESOURCE_ALIAS atp.ico)
qt_add_resources(atp_studio "brand" PREFIX "/" FILES resources/atp.ico)
if (WIN32)
    target_sources(atp_studio PRIVATE resources/atp_studio.rc)
endif ()

# windeployqt places the Qt DLLs next to the binary, so launching from the file manager or the IDE
# works without adding the Qt directory to PATH by hand.
if (WIN32)
    find_program(ATP_WINDEPLOYQT windeployqt HINTS "${Qt6_DIR}/../../../bin")
    if (ATP_WINDEPLOYQT)
        add_custom_command(TARGET atp_studio POST_BUILD
                COMMAND "${ATP_WINDEPLOYQT}" --no-translations --no-compiler-runtime
                        "$<TARGET_FILE:atp_studio>"
                COMMENT "windeployqt: Qt runtime next to atp_studio")
    endif ()
endif ()
