# The application is a thin main over atp_runtime: load the config, build the pipeline, run until
# Ctrl+C.
add_executable(atp_app main.cpp)
target_link_libraries(atp_app PRIVATE atp_runtime ${CMAKE_DL_LIBS})

# A live example: the configs and the demo plugin are placed next to the binary, so that
# "atp_app config/demo.json" works straight from the build directory. A separate custom target
# rather than a POST_BUILD step of atp_app: rebuilding the plugin alone does not relink the
# application, and POST_BUILD would leave a stale DLL in config/. The target runs on every build,
# which copy_if_different makes free.
add_dependencies(atp_app atp_demo_plugin)
add_custom_target(atp_app_demo_assets ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:atp_app>/config
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/config/demo.json
                ${CMAKE_CURRENT_SOURCE_DIR}/config/demo_consumers.json
                ${CMAKE_CURRENT_SOURCE_DIR}/config/mcp_demo.json
                $<TARGET_FILE:atp_demo_plugin>
                $<TARGET_FILE_DIR:atp_app>/config
        DEPENDS atp_app atp_demo_plugin)
