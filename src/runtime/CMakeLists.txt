# Общий рантайм загрузки и запуска пайплайнов: дерево групп, раннер,
# загрузка плагинов. Header-only: приложение, studio и тесты подключают
# одну и ту же библиотеку.
add_library(atp_runtime INTERFACE)
target_include_directories(atp_runtime INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(atp_runtime INTERFACE atp_platform)
