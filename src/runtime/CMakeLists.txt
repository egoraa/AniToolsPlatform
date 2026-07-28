include(FetchContent)

# JSON parser for the config; its sources land in external/ by the same mechanism as googletest.
FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
        DOWNLOAD_DIR ${CMAKE_SOURCE_DIR}/external/downloads
        SOURCE_DIR ${CMAKE_SOURCE_DIR}/external/nlohmann_json-src
)
FetchContent_MakeAvailable(nlohmann_json)

# The shared runtime for loading and running pipelines: the group tree, the runner, plugin loading,
# the config and building a pipeline from it. Header-only, so the application, studio and the tests
# all link the same library.
add_library(atp_runtime INTERFACE)
target_include_directories(atp_runtime INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(atp_runtime INTERFACE atp_platform nlohmann_json::nlohmann_json)
