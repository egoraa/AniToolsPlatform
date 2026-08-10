include(${CMAKE_SOURCE_DIR}/cmake/BuildLua.cmake)

atp_add_plugin(atp_lua_bridge C_ABI SOURCES
        plugin.cpp
        ctx_type.cpp
        descriptors.cpp
        errors.cpp
        instance.cpp
        script_file.cpp
        self_module.cpp
        state.cpp
        values.cpp)
target_link_libraries(atp_lua_bridge PRIVATE atp_warnings atp_lua)
target_compile_features(atp_lua_bridge PRIVATE cxx_std_23)

# The package has to sit where the bridge looks for it — lua/atp.lua next to the library — in the
# build tree as much as in an installation, or only an unpacked release would ever run a script.
#
# A node of the build graph rather than a POST_BUILD step, and keyed on the package file itself: a
# POST_BUILD command runs only when its target is relinked, so editing the package alone left the copy
# beside the library stale with nothing to say so. The bridge then loaded a package older than its
# source, which a test cannot tell from the change simply not working.
# The output is a stamp because OUTPUT admits no target-dependent generator expression, so the
# destination can only be spelled in the command; it carries $<CONFIG> because a multi-config
# generator gives every configuration its own directory to copy into, and one shared stamp would
# leave the second configuration with no package at all.
set(ATP_LUA_PACKAGE_STAMP ${CMAKE_CURRENT_BINARY_DIR}/atp_lua_package_$<CONFIG>.stamp)
add_custom_command(OUTPUT ${ATP_LUA_PACKAGE_STAMP}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/package/atp.lua
                $<TARGET_FILE_DIR:atp_lua_bridge>/lua/atp.lua
        COMMAND ${CMAKE_COMMAND} -E touch ${ATP_LUA_PACKAGE_STAMP}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/package/atp.lua
        COMMENT "atp package next to atp_lua_bridge")
add_custom_target(atp_lua_package DEPENDS ${ATP_LUA_PACKAGE_STAMP})
add_dependencies(atp_lua_bridge atp_lua_package)

if (ATP_INSTALL)
    # The bridge is a plugin and installs like one, beside the others. Unlike the Python bridge it
    # brings no runtime of its own to install: the interpreter is inside the file.
    install(TARGETS atp_lua_bridge
            LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}/${ATP_PLUGIN_DIRNAME} COMPONENT applications)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/package/atp.lua
            DESTINATION ${CMAKE_INSTALL_BINDIR}/${ATP_PLUGIN_DIRNAME}/lua COMPONENT applications)
endif ()
