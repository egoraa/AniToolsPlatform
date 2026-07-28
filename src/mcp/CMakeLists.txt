# The MCP server's logic is a header-only library, which the tests link directly; the executable is
# nothing but the stdio loop over it.
add_library(atp_mcp_lib INTERFACE)
target_include_directories(atp_mcp_lib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(atp_mcp_lib INTERFACE atp_studio_lib)

# Headers are globbed for IDE visibility only, as in the root CMakeLists.
file(GLOB_RECURSE ATP_MCP_HEADERS CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/include/atp/mcp/*.hpp)

# The executable is the stdio loop and nothing else; everything it does lives in atp_mcp_lib.
add_executable(atp_mcp main.cpp ${ATP_MCP_HEADERS})
target_link_libraries(atp_mcp PRIVATE atp_mcp_lib ${CMAKE_DL_LIBS})
