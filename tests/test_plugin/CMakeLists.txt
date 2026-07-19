# Тестовые плагины для module_loader. Скрытая видимость по умолчанию —
# наружу торчат только символы контракта (на MSVC пресет — no-op,
# там экспорт и так только явный).
add_library(test_plugin SHARED plugin.cpp)
target_link_libraries(test_plugin PRIVATE atp_platform)
set_target_properties(test_plugin PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON)

add_library(test_plugin_bad_abi SHARED plugin_bad_abi.cpp)
target_link_libraries(test_plugin_bad_abi PRIVATE atp_platform)
set_target_properties(test_plugin_bad_abi PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON)

add_library(test_plugin_empty SHARED plugin_empty.cpp)
target_link_libraries(test_plugin_empty PRIVATE atp_platform)
set_target_properties(test_plugin_empty PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON)
