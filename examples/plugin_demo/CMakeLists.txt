# Пример-двойник: один и тот же модуль (counter_modules.hpp) слинкован
# в монолитный хост и собран плагином для динамического хоста.
add_library(atp_demo_plugin SHARED plugin.cpp)
target_link_libraries(atp_demo_plugin PRIVATE atp_platform)
# PREFIX ""/OUTPUT_NAME: имя файла не зависит от тулчейна — конфиги
# ссылаются на одно имя под MinGW, MSVC и Linux.
set_target_properties(atp_demo_plugin PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        PREFIX ""
        OUTPUT_NAME "atp_demo_plugin")

add_executable(atp_host_static host_static.cpp)
target_link_libraries(atp_host_static PRIVATE atp_platform)

add_executable(atp_host_dynamic host_dynamic.cpp)
target_link_libraries(atp_host_dynamic PRIVATE atp_host ${CMAKE_DL_LIBS})
add_dependencies(atp_host_dynamic atp_demo_plugin)
target_compile_definitions(atp_host_dynamic PRIVATE
        ATP_DEMO_PLUGIN="$<PATH:CMAKE_PATH,$<TARGET_FILE:atp_demo_plugin>>")
