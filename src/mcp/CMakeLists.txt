add_library(atp_mcp_lib INTERFACE)
target_include_directories(atp_mcp_lib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# The version the server reports over the wire is the project's, carried in rather than spelled a
# second time: a literal in the header drifts from project() silently, and the wire is where that
# would be seen last. INTERFACE because atp_mcp_lib is header-only, so the definition has to reach
# every unit that includes server.hpp -- which is every unit that can, the include directory coming
# from this same target.
target_compile_definitions(atp_mcp_lib INTERFACE ATP_MCP_VERSION="${PROJECT_VERSION}")
target_link_libraries(atp_mcp_lib INTERFACE atp_studio_lib nlohmann_json::nlohmann_json)

file(GLOB_RECURSE ATP_MCP_HEADERS CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/include/atp/mcp/*.hpp)

add_executable(atp_mcp main.cpp ${ATP_MCP_HEADERS})
target_link_libraries(atp_mcp PRIVATE atp_mcp_lib atp_warnings ${CMAKE_DL_LIBS})

if (ATP_INSTALL)
    install(TARGETS atp_mcp RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT applications)
endif ()
