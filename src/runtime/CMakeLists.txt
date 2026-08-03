include(FetchContent)

FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
        DOWNLOAD_DIR ${CMAKE_SOURCE_DIR}/external/downloads
        SOURCE_DIR ${CMAKE_SOURCE_DIR}/external/nlohmann_json-src
        SYSTEM
)
FetchContent_MakeAvailable(nlohmann_json)

add_library(atp_runtime INTERFACE)
target_include_directories(atp_runtime INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
# ws2_32 comes with the runtime because socket_platform.hpp lives here: the MCP server, the studio's
# client and the tests all open sockets, and all three link atp_runtime already. Putting it on the
# MCP library instead would leave the studio — which must not depend on the MCP layer — without it.
target_link_libraries(atp_runtime INTERFACE atp_platform nlohmann_json::nlohmann_json
        $<$<PLATFORM_ID:Windows>:ws2_32>)
