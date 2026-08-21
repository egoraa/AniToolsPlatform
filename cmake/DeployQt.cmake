# Puts the Qt runtime **and its plugins** next to a Windows executable.
#
# The distinction is the whole point. $<TARGET_RUNTIME_DLLS> copies what the binary links, and a QPA
# plugin is not linked — it is loaded at run time from a platforms/ directory beside the executable.
# A Qt program without one dies inside QApplication's constructor with "no Qt platform plugin could be
# initialized", and on Windows that arrives as a modal dialog: unattended, the process simply stops
# responding. atp_ui_tests spent its whole life in that state (#41) — every suite that built a
# QApplication looked like it hung, while the suites that build none passed.
#
# windeployqt deploys the platform plugin it expects the program to use, which on Windows is qwindows.
# A program that asks for another one — atp_ui_tests forces offscreen in tests/ui/qt_app.hpp — has to
# say so, hence the trailing arguments. They are Qt's own imported plugin targets rather than paths,
# so the right debug or release variant follows from the build type instead of being spelled out.
#
#     atp_deploy_qt(atp_ui_tests Qt6::QOffscreenIntegrationPlugin)
function(atp_deploy_qt target)
    if (NOT WIN32)
        return()
    endif ()

    find_program(ATP_WINDEPLOYQT windeployqt HINTS "${Qt6_DIR}/../../../bin")

    # One deployment per output directory. atp_studio and atp_ui_tests share bin/, and windeployqt
    # writes the same files from both — concurrently, under a parallel generator, which costs a
    # truncated DLL rather than a wasted copy. The first target to ask for a directory owns the
    # deployment; a later one gets a dependency on the owner instead, which is enough because the
    # owner's POST_BUILD is what does the work. The extra plugins below are copied by every caller
    # regardless: they are what this caller needs and not what the owner asked for.
    #
    # The key is the directory as it is known at configure time — the property, or the variable
    # behind it. $<TARGET_FILE_DIR:...> cannot be compared here and does not have to be: two targets
    # with the same RUNTIME_OUTPUT_DIRECTORY resolve it the same way.
    get_target_property(target_dir ${target} RUNTIME_OUTPUT_DIRECTORY)
    if (NOT target_dir)
        set(target_dir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    endif ()

    get_property(deployed_dirs GLOBAL PROPERTY ATP_QT_DEPLOYED_DIRS)
    get_property(deployed_owners GLOBAL PROPERTY ATP_QT_DEPLOYED_OWNERS)
    list(FIND deployed_dirs "${target_dir}" deployed_at)

    if (deployed_at GREATER_EQUAL 0)
        list(GET deployed_owners ${deployed_at} owner)
        add_dependencies(${target} ${owner})
        message(STATUS "Qt runtime for ${target} is deployed by ${owner} into the same directory")
    elseif (ATP_WINDEPLOYQT)
        add_custom_command(TARGET ${target} POST_BUILD
                COMMAND "${ATP_WINDEPLOYQT}" --no-translations --no-compiler-runtime "$<TARGET_FILE:${target}>"
                COMMENT "windeployqt: Qt runtime and plugins next to ${target}")
        set_property(GLOBAL APPEND PROPERTY ATP_QT_DEPLOYED_DIRS "${target_dir}")
        set_property(GLOBAL APPEND PROPERTY ATP_QT_DEPLOYED_OWNERS "${target}")
    else ()
        message(WARNING
                "windeployqt was not found next to Qt6_DIR — ${target} will have no Qt runtime beside it "
                "and cannot run outside an IDE that sets the Qt environment itself")
    endif ()

    # Qt's imported plugin targets are directory scoped, and find_package(Qt6) ran in src/studio rather
    # than wherever this is called from, so they have to be asked for again here. The package is
    # already cached, which makes the second call a lookup and not a search.
    if (ARGN)
        find_package(Qt6 COMPONENTS Gui QUIET)
    endif ()

    # After windeployqt, so that the directory it creates is already there and nothing it copies can
    # overwrite a plugin asked for here.
    foreach (plugin IN LISTS ARGN)
        if (NOT TARGET ${plugin})
            message(WARNING "${target}: Qt platform plugin target ${plugin} does not exist")
            continue()
        endif ()
        add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/platforms"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "$<TARGET_FILE:${plugin}>" "$<TARGET_FILE_DIR:${target}>/platforms"
                COMMENT "${plugin} next to ${target}")
    endforeach ()
endfunction()
