atp_add_plugin(atp_demo_plugin SOURCES plugin.cpp)
target_link_libraries(atp_demo_plugin PRIVATE atp_warnings)

add_executable(atp_host_static host_static.cpp)
target_link_libraries(atp_host_static PRIVATE atp_platform atp_warnings)

add_executable(atp_host_dynamic host_dynamic.cpp)
target_link_libraries(atp_host_dynamic PRIVATE atp_runtime atp_warnings ${CMAKE_DL_LIBS})
add_dependencies(atp_host_dynamic atp_demo_plugin)
target_compile_definitions(atp_host_dynamic PRIVATE
        ATP_DEMO_PLUGIN="$<PATH:CMAKE_PATH,$<TARGET_FILE:atp_demo_plugin>>")
