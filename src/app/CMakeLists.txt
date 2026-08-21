add_executable(atp_app main.cpp)
target_link_libraries(atp_app PRIVATE atp_runtime atp_mcp_lib atp_warnings ${CMAKE_DL_LIBS})

add_dependencies(atp_app atp_demo_plugin atp_c_demo_plugin)

# The configs alone: the plugins they name are built straight into ATP_PLUGIN_DIRNAME beside atp_app
# (cmake/AniToolsPlatformPluginHelpers.cmake), so there is nothing left to copy there. The split
# between the two directories is still the point — config/ holds configs, ATP_PLUGIN_DIRNAME holds
# loadable code — because a config reaches a plugin as ../plugins/<name>, relative to the config's own
# directory, and that resolves only while the build tree is shaped like an installation.
add_custom_target(atp_app_demo_assets ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:atp_app>/config
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/config/demo.json
                ${CMAKE_CURRENT_SOURCE_DIR}/config/demo_consumers.json
                ${CMAKE_CURRENT_SOURCE_DIR}/config/mcp_demo.json
                ${CMAKE_CURRENT_SOURCE_DIR}/config/c_demo.json
                ${CMAKE_CURRENT_SOURCE_DIR}/config/rig.ini
                $<TARGET_FILE_DIR:atp_app>/config
        DEPENDS atp_app atp_demo_plugin atp_c_demo_plugin)

# The bridge and its package need no copy: the bridge builds into ATP_PLUGIN_DIRNAME beside atp_app,
# and src/bridges/python writes the package to python/atp next to the bridge, which is that same
# directory. What is left is the sample pipeline — its config and the two scripts it names — which
# belongs to this demo rather than to the bridge: `atp_app config/pipeline_python.json` has to mean the
# same thing in the build tree as in the documentation. The scripts go beside the bridge because that
# is where the bridge looks for them, and the directory is created here rather than assumed: the
# package copy and this target run in no particular order.
if (TARGET atp_python_bridge)
    add_dependencies(atp_app atp_python_bridge)
    add_custom_target(atp_app_python_assets ALL
            COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:atp_app>/${ATP_PLUGIN_DIRNAME}/python
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${CMAKE_SOURCE_DIR}/templates/plugin_python/pipeline.json
                    $<TARGET_FILE_DIR:atp_app>/config/pipeline_python.json
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${CMAKE_SOURCE_DIR}/templates/plugin_python/averager.py
                    ${CMAKE_SOURCE_DIR}/templates/plugin_python/packer.py
                    $<TARGET_FILE_DIR:atp_app>/${ATP_PLUGIN_DIRNAME}/python
            DEPENDS atp_app atp_python_bridge)
    # The config lands in the directory atp_app_demo_assets creates, so it has to run after it.
    add_dependencies(atp_app_python_assets atp_app_demo_assets)
endif ()

# The same arrangement for Lua, with one difference worth noticing: there is nothing beside the bridge
# but its own package, because the interpreter is inside the file. A machine that can run atp_app can
# run a Lua module.
if (TARGET atp_lua_bridge)
    add_dependencies(atp_app atp_lua_bridge)
    add_custom_target(atp_app_lua_assets ALL
            COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:atp_app>/${ATP_PLUGIN_DIRNAME}/lua
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${CMAKE_SOURCE_DIR}/templates/plugin_lua/pipeline.json
                    $<TARGET_FILE_DIR:atp_app>/config/pipeline_lua.json
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${CMAKE_SOURCE_DIR}/templates/plugin_lua/scale.lua
                    ${CMAKE_SOURCE_DIR}/templates/plugin_lua/packer.lua
                    $<TARGET_FILE_DIR:atp_app>/${ATP_PLUGIN_DIRNAME}/lua
            DEPENDS atp_app atp_lua_bridge)
    add_dependencies(atp_app_lua_assets atp_app_demo_assets)
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
            ${CMAKE_CURRENT_SOURCE_DIR}/config/rig.ini
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
    if (TARGET atp_lua_bridge)
        install(FILES
                ${CMAKE_SOURCE_DIR}/templates/plugin_lua/scale.lua
                ${CMAKE_SOURCE_DIR}/templates/plugin_lua/packer.lua
                DESTINATION ${CMAKE_INSTALL_BINDIR}/${ATP_PLUGIN_DIRNAME}/lua COMPONENT applications)
        install(FILES
                ${CMAKE_SOURCE_DIR}/templates/plugin_lua/pipeline.json
                DESTINATION ${CMAKE_INSTALL_BINDIR}/config
                RENAME pipeline_lua.json
                COMPONENT applications)
    endif ()
endif ()
