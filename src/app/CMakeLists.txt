include(FetchContent)

# JSON-парсер конфига; исходники — в external/ тем же механизмом, что googletest.
FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
        DOWNLOAD_DIR ${CMAKE_SOURCE_DIR}/external/downloads
        SOURCE_DIR ${CMAKE_SOURCE_DIR}/external/nlohmann_json-src
)
FetchContent_MakeAvailable(nlohmann_json)

# Логика приложения — header-only библиотека: тесты подключают её напрямую,
# исполняемый файл остаётся тонким main.
add_library(atp_app_lib INTERFACE)
target_include_directories(atp_app_lib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(atp_app_lib INTERFACE atp_host nlohmann_json::nlohmann_json)

file(GLOB_RECURSE ATP_APP_HEADERS CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/atp/app/*.hpp)

add_executable(atp_app main.cpp)
target_link_libraries(atp_app PRIVATE atp_app_lib ${CMAKE_DL_LIBS})
