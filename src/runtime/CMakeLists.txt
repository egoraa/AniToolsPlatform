include(FetchContent)

FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
        DOWNLOAD_DIR ${CMAKE_SOURCE_DIR}/external/downloads
        SOURCE_DIR ${CMAKE_SOURCE_DIR}/external/nlohmann_json-src
)
FetchContent_MakeAvailable(nlohmann_json)

add_library(atp_runtime INTERFACE)
target_include_directories(atp_runtime INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(atp_runtime INTERFACE atp_platform nlohmann_json::nlohmann_json)
