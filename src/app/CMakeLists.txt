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
target_link_libraries(atp_app_lib INTERFACE atp_runtime nlohmann_json::nlohmann_json)

file(GLOB_RECURSE ATP_APP_HEADERS CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/atp/app/*.hpp)

add_executable(atp_app main.cpp)
target_link_libraries(atp_app PRIVATE atp_app_lib ${CMAKE_DL_LIBS})

# Живой пример: конфиги и демо-плагин кладутся рядом с бинарём —
# запуск «atp_app config/demo.json» работает из каталога сборки.
# Отдельная custom-цель, а не POST_BUILD у atp_app: пересборка одного
# плагина не перелинковывает приложение, и POST_BUILD оставил бы в
# config/ устаревший DLL. Цель выполняется каждой сборкой,
# copy_if_different делает это бесплатным.
add_dependencies(atp_app atp_demo_plugin)
add_custom_target(atp_app_demo_assets ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:atp_app>/config
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/config/demo.json
                ${CMAKE_CURRENT_SOURCE_DIR}/config/demo_consumers.json
                $<TARGET_FILE:atp_demo_plugin>
                $<TARGET_FILE_DIR:atp_app>/config
        DEPENDS atp_app atp_demo_plugin)
