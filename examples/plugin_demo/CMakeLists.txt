atp_add_plugin(atp_demo_plugin SOURCES plugin.cpp)
target_link_libraries(atp_demo_plugin PRIVATE atp_warnings)

if (ATP_INSTALL)
    # The demo plugin is installed here, where it is declared, rather than by src/app, which is only
    # its first consumer: the sample configs name it, but so does templates/plugin/pipeline.json, and
    # a rule for this target hidden in another directory is a rule nobody looking at this one finds.
    # A MODULE library is a LIBRARY destination on every platform, Windows included.
    install(TARGETS atp_demo_plugin
            LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}/${ATP_PLUGIN_DIRNAME} COMPONENT applications)
endif ()

add_executable(atp_host_static host_static.cpp)
target_link_libraries(atp_host_static PRIVATE atp_platform atp_warnings)

add_executable(atp_host_dynamic host_dynamic.cpp)
target_link_libraries(atp_host_dynamic PRIVATE atp_runtime atp_warnings ${CMAKE_DL_LIBS})
add_dependencies(atp_host_dynamic atp_demo_plugin)
target_compile_definitions(atp_host_dynamic PRIVATE
        ATP_DEMO_PLUGIN="$<PATH:CMAKE_PATH,$<TARGET_FILE:atp_demo_plugin>>")
