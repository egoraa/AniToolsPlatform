add_executable(atp_app main.cpp)
target_link_libraries(atp_app PRIVATE atp_runtime ${CMAKE_DL_LIBS})

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
