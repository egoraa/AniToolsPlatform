add_executable(atp_app main.cpp)
target_link_libraries(atp_app PRIVATE atp_runtime atp_mcp_lib atp_warnings ${CMAKE_DL_LIBS})

add_dependencies(atp_app atp_demo_plugin atp_c_demo_plugin)

# Two directories rather than one, and the split is the point: config/ holds configs, ATP_PLUGIN_DIRNAME
# holds loadable code. A config reaches a plugin as ../plugins/<name> — plugin paths resolve relative to
# the config's own directory — so the two names have to be laid out here exactly as they are in an
# installation, or `atp_app config/demo.json` would mean two different things in the two trees.
add_custom_target(atp_app_demo_assets ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:atp_app>/config
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/config/demo.json
                ${CMAKE_CURRENT_SOURCE_DIR}/config/demo_consumers.json
                ${CMAKE_CURRENT_SOURCE_DIR}/config/mcp_demo.json
                ${CMAKE_CURRENT_SOURCE_DIR}/config/c_demo.json
                $<TARGET_FILE_DIR:atp_app>/config
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:atp_app>/${ATP_PLUGIN_DIRNAME}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:atp_demo_plugin>
                $<TARGET_FILE:atp_c_demo_plugin>
                $<TARGET_FILE_DIR:atp_app>/${ATP_PLUGIN_DIRNAME}
        DEPENDS atp_app atp_demo_plugin atp_c_demo_plugin)

# The bridge rides along with the demo assets for the same reason the demo plugins do: `atp_app
# config/pipeline_python.json` has to mean the same thing in the build tree as in the documentation.
# The package needs its own copy step because it is a directory, and the bridge looks for it at
# python/atp next to itself — which is why the script goes beside the bridge and not beside the config.
# The directory is created here as well as in the target above: the two run in no particular order.
if (TARGET atp_python_bridge)
    add_dependencies(atp_app atp_python_bridge)
    add_custom_target(atp_app_python_assets ALL
            COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:atp_app>/${ATP_PLUGIN_DIRNAME}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:atp_python_bridge>
                    $<TARGET_FILE_DIR:atp_app>/${ATP_PLUGIN_DIRNAME}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${CMAKE_SOURCE_DIR}/templates/plugin_python/pipeline.json
                    $<TARGET_FILE_DIR:atp_app>/config/pipeline_python.json
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    $<TARGET_FILE_DIR:atp_python_bridge>/python
                    $<TARGET_FILE_DIR:atp_app>/${ATP_PLUGIN_DIRNAME}/python
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${CMAKE_SOURCE_DIR}/templates/plugin_python/averager.py
                    ${CMAKE_SOURCE_DIR}/templates/plugin_python/packer.py
                    $<TARGET_FILE_DIR:atp_app>/${ATP_PLUGIN_DIRNAME}/python
            DEPENDS atp_app atp_python_bridge)
endif ()

if (ATP_INSTALL)
    # The sample configs go under bin/config for the same reason they are copied there in the build
    # tree: `atp_app config/demo.json` has to mean the same thing in an unpacked release as it does in
    # the documentation. A release without them would install a host with nothing to host.
    #
    # The plugins those configs name are installed by the directories that declare them —
    # examples/plugin_demo, examples/plugin_c_demo, src/bridges/python — into bin/${ATP_PLUGIN_DIRNAME}.
    # averager.py is the exception that proves the rule: it is not the bridge but an asset of the
    # sample pipeline below, and it lands beside the bridge only because that is where the bridge looks.
    install(TARGETS atp_app RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT applications)
    install(FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/config/demo.json
            ${CMAKE_CURRENT_SOURCE_DIR}/config/demo_consumers.json
            ${CMAKE_CURRENT_SOURCE_DIR}/config/mcp_demo.json
            ${CMAKE_CURRENT_SOURCE_DIR}/config/c_demo.json
            DESTINATION ${CMAKE_INSTALL_BINDIR}/config
            COMPONENT applications)
    if (TARGET atp_python_bridge)
        install(FILES
                ${CMAKE_SOURCE_DIR}/templates/plugin_python/averager.py
                ${CMAKE_SOURCE_DIR}/templates/plugin_python/packer.py
                DESTINATION ${CMAKE_INSTALL_BINDIR}/${ATP_PLUGIN_DIRNAME}/python COMPONENT applications)
        install(FILES
                ${CMAKE_SOURCE_DIR}/templates/plugin_python/pipeline.json
                DESTINATION ${CMAKE_INSTALL_BINDIR}/config
                RENAME pipeline_python.json
                COMPONENT applications)
    endif ()
endif ()
