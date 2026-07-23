include(FetchContent)

# JSON-парсер конфига; исходники — в external/ тем же механизмом, что googletest.
FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
        DOWNLOAD_DIR ${CMAKE_SOURCE_DIR}/external/downloads
        SOURCE_DIR ${CMAKE_SOURCE_DIR}/external/nlohmann_json-src
)
FetchContent_MakeAvailable(nlohmann_json)

# Общий рантайм загрузки и запуска пайплайнов: дерево групп, раннер, загрузка
# плагинов, конфиг и сборка пайплайна по нему. Header-only: приложение, studio
# и тесты подключают одну и ту же библиотеку.
add_library(atp_runtime INTERFACE)
target_include_directories(atp_runtime INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(atp_runtime INTERFACE atp_platform nlohmann_json::nlohmann_json)
