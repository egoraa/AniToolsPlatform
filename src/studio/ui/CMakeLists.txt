qt_standard_project_setup()

find_package(Qt6 COMPONENTS Svg REQUIRED)

file(GLOB_RECURSE ATP_STUDIO_UI_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp)
list(REMOVE_ITEM ATP_STUDIO_UI_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)

qt_add_library(atp_studio_ui STATIC ${ATP_STUDIO_UI_SOURCES})
target_include_directories(atp_studio_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(atp_studio_ui PUBLIC atp_studio_lib Qt6::Widgets Qt6::Svg ${CMAKE_DL_LIBS})
target_link_libraries(atp_studio_ui PRIVATE atp_warnings)

qt_add_executable(atp_studio main.cpp)
target_link_libraries(atp_studio PRIVATE atp_studio_ui atp_warnings)

# Only Apple reads this property, and there it is a requirement rather than a preference: Qt refuses
# to deploy its runtime into anything but a bundle ("Darwin, shared Qt libs, non-bundle app"), so
# without it the macOS package ships without Qt. qt_add_executable does not set it.
set_target_properties(atp_studio PROPERTIES MACOSX_BUNDLE TRUE)

target_compile_definitions(atp_studio_ui PRIVATE ATP_STUDIO_VERSION="${PROJECT_VERSION}")

set_source_files_properties(resources/atp.ico PROPERTIES QT_RESOURCE_ALIAS atp.ico)
qt_add_resources(atp_studio "brand" PREFIX "/" FILES resources/atp.ico)
if (WIN32)
    target_sources(atp_studio PRIVATE resources/atp_studio.rc)
endif ()

file(GLOB ATP_STUDIO_ICONS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/*.svg)
foreach (icon IN LISTS ATP_STUDIO_ICONS)
    cmake_path(GET icon FILENAME icon_name)
    set_source_files_properties(${icon} PROPERTIES QT_RESOURCE_ALIAS icons/${icon_name})
endforeach ()
qt_add_resources(atp_studio "icons" PREFIX "/" FILES ${ATP_STUDIO_ICONS})

atp_deploy_qt(atp_studio)

# Every bridge lands in ATP_PLUGIN_DIRNAME beside atp_studio, which is where an installation has it
# relative to the executable (bin/ and bin/plugins/). Studio needs it there for two different reasons:
# a person can add that directory as a search directory and get script modules at all, and
# `File > New module...` copies the bridge and its package into the folder it is creating a module in
# — find_bridge_source looks exactly here before it considers any loaded bridge. Without these steps
# the feature works in a package and not in the build tree, which is the wrong way round for the tree
# where it is developed. The package follows its bridge because a bridge looks for it in the language's
# own subdirectory next to itself. Forgetting to rebuild one of these targets leaves a stale bridge
# beside the studio that it will nonetheless treat as the platform's own.
if (TARGET atp_python_bridge)
    add_dependencies(atp_studio atp_python_bridge)
    add_custom_target(atp_studio_python_assets ALL
            COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:atp_studio>/${ATP_PLUGIN_DIRNAME}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:atp_python_bridge>
                    $<TARGET_FILE_DIR:atp_studio>/${ATP_PLUGIN_DIRNAME}
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    $<TARGET_FILE_DIR:atp_python_bridge>/python
                    $<TARGET_FILE_DIR:atp_studio>/${ATP_PLUGIN_DIRNAME}/python
            DEPENDS atp_studio atp_python_bridge)
endif ()

if (TARGET atp_lua_bridge)
    add_dependencies(atp_studio atp_lua_bridge)
    add_custom_target(atp_studio_lua_assets ALL
            COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:atp_studio>/${ATP_PLUGIN_DIRNAME}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:atp_lua_bridge>
                    $<TARGET_FILE_DIR:atp_studio>/${ATP_PLUGIN_DIRNAME}
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    $<TARGET_FILE_DIR:atp_lua_bridge>/lua
                    $<TARGET_FILE_DIR:atp_studio>/${ATP_PLUGIN_DIRNAME}/lua
            DEPENDS atp_studio atp_lua_bridge)
endif ()

if (ATP_INSTALL)
    install(TARGETS atp_studio
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT applications
            BUNDLE DESTINATION . COMPONENT applications)

    # atp_deploy_qt above serves the build tree and copies beside the executable there; the install
    # tree is a different directory that it never sees. Qt's own generator is used here instead of a
    # second windeployqt call because it is the one thing that speaks all three platforms —
    # windeployqt, macdeployqt and the Linux case, which has no tool and needs the libraries and the
    # plugin directories placed by hand.
    # NO_TRANSLATIONS to match the build tree, where atp_deploy_qt passes --no-translations: the
    # interface is written in English and does not load any, so the alternative is thirty-odd .qm
    # files that lrelease has to build first and nothing ever reads.
    #
    # NO_UNSUPPORTED_PLATFORM_ERROR is deliberately *not* passed, though it was at first. It turns a
    # platform Qt cannot deploy on into silence, and silence here is the worst outcome: the macOS run
    # of the release workflow packaged an 856 KB disk image with no Qt in it and reported success,
    # while the same commit produced 23 MB on Windows and 30 MB on Linux. A configure-time error names
    # the problem; a package that installs and then fails to start does not.
    qt_generate_deploy_app_script(
            TARGET atp_studio
            OUTPUT_SCRIPT atp_studio_deploy_script
            NO_TRANSLATIONS)
    install(SCRIPT ${atp_studio_deploy_script})
endif ()
