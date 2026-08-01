add_library(atp_demo_plugin SHARED plugin.cpp)
target_link_libraries(atp_demo_plugin PRIVATE atp_platform)
set_target_properties(atp_demo_plugin PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        PREFIX ""
        OUTPUT_NAME "atp_demo_plugin")

add_executable(atp_host_static host_static.cpp)
target_link_libraries(atp_host_static PRIVATE atp_platform)

add_executable(atp_host_dynamic host_dynamic.cpp)
target_link_libraries(atp_host_dynamic PRIVATE atp_runtime ${CMAKE_DL_LIBS})
add_dependencies(atp_host_dynamic atp_demo_plugin)
target_compile_definitions(atp_host_dynamic PRIVATE
        ATP_DEMO_PLUGIN="$<PATH:CMAKE_PATH,$<TARGET_FILE:atp_demo_plugin>>")
