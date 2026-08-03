add_library(atp_mcp_lib INTERFACE)
target_include_directories(atp_mcp_lib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(atp_mcp_lib INTERFACE atp_studio_lib)

file(GLOB_RECURSE ATP_MCP_HEADERS CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/include/atp/mcp/*.hpp)

add_executable(atp_mcp main.cpp ${ATP_MCP_HEADERS})
target_link_libraries(atp_mcp PRIVATE atp_mcp_lib atp_warnings ${CMAKE_DL_LIBS})

if (ATP_INSTALL)
    install(TARGETS atp_mcp RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT applications)
endif ()
