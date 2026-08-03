add_executable(atp_app main.cpp)
target_link_libraries(atp_app PRIVATE atp_runtime atp_mcp_lib atp_warnings ${CMAKE_DL_LIBS})

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

if (ATP_INSTALL)
    # The demo plugin and the configs go under bin/config for the same reason they are copied there in
    # the build tree: `atp_app config/demo.json` has to mean the same thing in an unpacked release as
    # it does in the documentation. A release without them would install a host with nothing to host.
    install(TARGETS atp_app atp_demo_plugin
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT applications
            LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}/config COMPONENT applications)
    install(FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/config/demo.json
            ${CMAKE_CURRENT_SOURCE_DIR}/config/demo_consumers.json
            ${CMAKE_CURRENT_SOURCE_DIR}/config/mcp_demo.json
            DESTINATION ${CMAKE_INSTALL_BINDIR}/config
            COMPONENT applications)
endif ()
