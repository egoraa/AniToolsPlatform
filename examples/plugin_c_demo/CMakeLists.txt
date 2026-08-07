atp_add_plugin(atp_c_demo_plugin C_ABI SOURCES plugin.c scaler_module.c)
target_link_libraries(atp_c_demo_plugin PRIVATE atp_warnings)

if (ATP_INSTALL)
    install(TARGETS atp_c_demo_plugin
            LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}/${ATP_PLUGIN_DIRNAME} COMPONENT applications)
endif ()
