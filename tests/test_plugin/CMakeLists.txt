# Test plugins for module_loader. Hidden visibility by default leaves only the contract symbols
# exposed (on MSVC the preset is a no-op, since exports are explicit there anyway).
# PREFIX ""/OUTPUT_NAME keep the file name toolchain-independent, so configs and tests reference one
# name under MinGW, MSVC and Linux alike.
add_library(test_plugin SHARED plugin.cpp)
target_link_libraries(test_plugin PRIVATE atp_platform)
set_target_properties(test_plugin PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        PREFIX ""
        OUTPUT_NAME "test_plugin")

add_library(test_plugin_bad_abi SHARED plugin_bad_abi.cpp)
target_link_libraries(test_plugin_bad_abi PRIVATE atp_platform)
set_target_properties(test_plugin_bad_abi PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        PREFIX ""
        OUTPUT_NAME "test_plugin_bad_abi")

add_library(test_plugin_empty SHARED plugin_empty.cpp)
target_link_libraries(test_plugin_empty PRIVATE atp_platform)
set_target_properties(test_plugin_empty PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        PREFIX ""
        OUTPUT_NAME "test_plugin_empty")
