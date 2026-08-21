# These seven stay out of bin/plugins, and the reason is a contract of the suites rather than tidiness.
# Three of them are deliberately broken — a bad ABI number, a bad descriptor, no entry points — and
# several suites take the directory of one fixture and hand it to a scanner as a search directory
# (tests/mcp/catalog_tools_tests.cpp, tests/mcp/options_tests.cpp, tests/studio/module_manager_tests.cpp).
# What those tests then discover is whatever the directory holds, so sharing it with the bridges and
# the demo plugins would make their expectations a function of which other targets were built. They
# are addressed by absolute path through the ATP_TEST_PLUGIN* definitions and never by directory name,
# so no test has to be told where this is.
set(ATP_PLUGIN_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

atp_add_plugin(test_plugin SOURCES plugin.cpp)
target_link_libraries(test_plugin PRIVATE atp_warnings)

atp_add_plugin(test_plugin_ports SOURCES plugin_ports.cpp)
target_link_libraries(test_plugin_ports PRIVATE atp_warnings)

atp_add_plugin(test_plugin_bad_abi SOURCES plugin_bad_abi.cpp)
target_link_libraries(test_plugin_bad_abi PRIVATE atp_warnings)

atp_add_plugin(test_plugin_empty SOURCES plugin_empty.cpp)
target_link_libraries(test_plugin_empty PRIVATE atp_warnings)

atp_add_plugin(test_plugin_c C_ABI SOURCES plugin_c.c)
target_link_libraries(test_plugin_c PRIVATE atp_warnings)

atp_add_plugin(test_plugin_c_bad_abi C_ABI SOURCES plugin_c_bad_abi.c)
target_link_libraries(test_plugin_c_bad_abi PRIVATE atp_warnings)

atp_add_plugin(test_plugin_c_bad_desc C_ABI SOURCES plugin_c_bad_desc.c)
target_link_libraries(test_plugin_c_bad_desc PRIVATE atp_warnings)
