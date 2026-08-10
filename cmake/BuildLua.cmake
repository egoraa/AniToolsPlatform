# PUC-Lua ships no CMakeLists, so the interpreter's target is declared here rather than by
# add_subdirectory. Three decisions worth reading before touching this file.
#
# The interpreter is vendored rather than looked for, and that is what the Lua bridge trades for the
# Python bridge's flexibility: Lua has no stable ABI across 5.x and no convention for shipping one, so
# a bridge built against whatever the machine happens to have would be a bridge that loads on that
# machine alone. The sources are 30k lines under a permissive license, which makes the other direction
# cheap — and it removes the entire class of failures where the plugin loads on the build machine and
# not on anyone else's.
#
# They are compiled as C++ on purpose. Lua built as C raises errors with longjmp, and a longjmp out of
# a C function whose caller has a C++ object with a destructor on the stack skips that destructor;
# every callback the bridge installs is exactly such a frame. Built as C++ the same mechanism becomes
# throw/catch, which unwinds correctly. Lua's headers carry no extern "C" guards, which is why
# lua_api.hpp includes them without one.
#
# Hidden visibility is not cosmetic either: the interpreter is linked statically into a plugin, and a
# second plugin in the same process would carry its own copy. Exported lua_* symbols would let the
# dynamic linker resolve one copy's calls into the other's state.
include(FetchContent)

FetchContent_Declare(
        lua
        URL https://www.lua.org/ftp/lua-5.4.8.tar.gz
        URL_HASH SHA256=4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae
        DOWNLOAD_DIR ${CMAKE_SOURCE_DIR}/external/downloads
        SOURCE_DIR ${CMAKE_SOURCE_DIR}/external/lua-src
        SYSTEM
)
FetchContent_MakeAvailable(lua)

# The two interpreter front-ends are the only sources here that are programs rather than library code,
# and each brings a main().
file(GLOB ATP_LUA_SOURCES ${lua_SOURCE_DIR}/src/*.c)
list(REMOVE_ITEM ATP_LUA_SOURCES ${lua_SOURCE_DIR}/src/lua.c ${lua_SOURCE_DIR}/src/luac.c)

add_library(atp_lua STATIC ${ATP_LUA_SOURCES})
set_source_files_properties(${ATP_LUA_SOURCES} PROPERTIES LANGUAGE CXX)
set_target_properties(atp_lua PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON)

# SYSTEM, because CI builds every job with ATP_WERROR=ON and these headers are not ours to fix: a
# diagnostic some compiler version decides to emit inside luaconf.h would fail a branch for a reason
# nobody in this repository can act on.
target_include_directories(atp_lua SYSTEM PUBLIC ${lua_SOURCE_DIR}/src)

if (APPLE)
    target_compile_definitions(atp_lua PRIVATE LUA_USE_MACOSX)
elseif (UNIX)
    target_compile_definitions(atp_lua PRIVATE LUA_USE_LINUX)
    target_link_libraries(atp_lua PRIVATE ${CMAKE_DL_LIBS} m)
endif ()
