cmake_minimum_required(VERSION 4.1)
project(AniToolsPlatform VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ATP_AUTO_INSTALL_QT "Download Qt via aqtinstall into external/ if not found" OFF)

option(ATP_BUILD_STUDIO "Build the studio GUI; the headless studio core and atp_mcp are built either way" ON)

# Off by default so an ordinary build, and every CI job, is unaffected — the same bargain as
# ATP_SANITIZER. A benchmark built Debug measures the debug runtime, so the numbers in
# docs/benchmarks.md come from a Release configuration and say so. The benchmarks live under tests/
# because they answer the same kind of question about the same code, and are added from there.
option(ATP_BUILD_BENCHMARKS "Build the google/benchmark micro-benchmarks" OFF)

# On by default and still costless without CPython: the bridge subdirectory returns early when the
# development files are missing, so a machine with no Python configures exactly as before.
option(ATP_BUILD_PYTHON_BRIDGE "Build the bridge that loads modules written in Python" ON)

# On by default, and unlike the Python bridge it has nothing to be absent: the interpreter is vendored
# and compiled into the plugin. So OFF does not mean "this machine cannot" but "this build does not
# want it" — and it is honoured before the subdirectory is added, which is what keeps the option from
# costing a download nobody asked for.
option(ATP_BUILD_LUA_BRIDGE "Build the bridge that loads modules written in Lua" ON)

# Off when this project is embedded in another one: install rules of a subproject land in the parent's
# package, which is never what the parent asked for.
option(ATP_INSTALL "Generate the install and export rules for the SDK" ${PROJECT_IS_TOP_LEVEL})

# CMake's own default is a system directory — /usr/local, or "C:/Program Files (x86)/AniToolsPlatform"
# on Windows — and installing there needs root or an elevated shell. A developer who presses "install"
# in an IDE has asked for none of that and gets a permission failure that CMake reports as "cannot make
# directory ... No such file or directory", which reads as a wrong path rather than as a refusal. So the
# default lands in the build tree instead, where a build already writes and where a plugin author's
# find_package(AniToolsPlatform) can be pointed with no privileges at all.
#
# CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT is what keeps this a *default* rather than a policy: CMake
# sets it only when the prefix was not given, and only in a build tree where it has not been set before,
# so -DCMAKE_INSTALL_PREFIX and `cmake --install --prefix` still win — which is how every CI job and the
# release workflow install, and why none of them sees this line. FORCE is required because the variable
# is already in the cache by the time this runs; without it the assignment would be ignored.
#
# Only when this project is the top level: a parent embedding it decides where its own installation goes.
if (PROJECT_IS_TOP_LEVEL AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/stage" CACHE PATH "Install prefix (defaults into the build tree)" FORCE)
    message(STATUS "Install prefix defaulted to ${CMAKE_INSTALL_PREFIX}; pass -DCMAKE_INSTALL_PREFIX to override")
endif ()

# Before atp_platform, whose INSTALL_INTERFACE spells CMAKE_INSTALL_INCLUDEDIR.
include(GNUInstallDirs)

# Loadable code — the demo plugins, the Python bridge, the scripts it reads — lands in a directory of
# its own, next to atp_app in the build tree and under bin/ in an installation, while config/ holds
# configs alone. One variable rather than a literal in each of the four places that name it, because a
# sample config reaches a plugin as ../plugins/<name> relative to itself, and that resolves only while
# both trees spell the directory the same way.
set(ATP_PLUGIN_DIRNAME plugins)

# One directory per kind of artifact, for the whole tree and the vendored dependencies with it —
# hence here, before the first add_subdirectory. Executables and the Qt runtime beside them in bin/,
# everything loadable in bin/${ATP_PLUGIN_DIRNAME}, static and import libraries in lib/. The layout is
# the installation's own (bin/, bin/config/, bin/plugins/), which is the point: a config reaches a
# plugin as ../plugins/<name> relative to itself, so `atp_app config/demo.json` means the same thing
# in the build tree as in an unpacked release only while the two trees are shaped alike. Left to
# CMake's default every target lands in the binary directory mirroring its source directory, which is
# what made every executable need its own copy of the plugins and its own copy of Qt.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# /Z7 rather than /Zi: a compile PDB is a type server CMake names per target only for a static library,
# so everything else shares cl's default one, and an object whose (GUID, age) stops matching it links
# under LNK4099 — public symbols kept, types, locals and line tables gone. Embedded also puts a static
# library's debug information inside the .lib. Rationale in full: docs/architecture.md.
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")

# A plugin is the one target that does not follow CMAKE_LIBRARY_OUTPUT_DIRECTORY: it is loaded by
# path at run time from the directory a config names, not linked, so it belongs next to the hosts
# rather than among the libraries. atp_add_plugin reads this and an out-of-tree build defines it
# nowhere, which is what keeps the installed helper behaving exactly as it does today.
#
# The generator expression in the multi-config branch is load-bearing rather than decorative. CMake
# appends /<Config> to a plain output directory under a multi-config generator and appends nothing
# when the value contains a generator expression, so the plain spelling would produce
# bin/plugins/Debug while the executables went to bin/Debug — and ../plugins would then resolve to
# nothing. Single-config generators append nothing either way, so there the plain spelling is right
# and $<CONFIG> would invent a subdirectory no executable is in.
get_property(ATP_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if (ATP_MULTI_CONFIG)
    set(ATP_PLUGIN_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/$<CONFIG>/${ATP_PLUGIN_DIRNAME})
else ()
    set(ATP_PLUGIN_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/${ATP_PLUGIN_DIRNAME})
endif ()

# UTF-8 everywhere, and this is a correctness switch rather than a convenience. Without it MSVC reads
# a source through the process code page and encodes narrow literals back through it, so the bytes of a
# literal depend on the machine that compiled it. The tree's own sources are UTF-8 and hold three
# non-ASCII code points (an em dash, an ellipsis and an arrow) — some of them inside real string
# literals, among them the MCP tool descriptions, which travel as JSON: nlohmann's dump is strict about
# UTF-8 and throws on invalid input, so a code page that does not round-trip those bytes turns into
# atp_mcp failing to answer at all. It currently works by accident on a 1252 machine and by nothing
# anywhere else. gcc and clang already default to UTF-8 on both ends and need no flag.
#
# Before the subdirectories on purpose: it has to reach the vendored dependencies too, or one tree
# would be compiled under two source encodings.
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8> $<$<C_COMPILER_ID:MSVC>:/utf-8>)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Warnings.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Sanitizers.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Format.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployQt.cmake)

add_library(atp_platform INTERFACE)

# The two interfaces name the same headers at two different times: BUILD_INTERFACE while this tree is
# the SDK, INSTALL_INTERFACE once it has been installed somewhere else. A plain path would be baked
# into the exported target and point at a directory that need not exist on the consumer's machine.
target_include_directories(atp_platform INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

# CMAKE_CXX_STANDARD above governs this project's own targets and reaches no consumer, so without
# this an out-of-tree plugin compiles the SDK under whatever default its compiler has and fails deep
# inside the headers. The requirement is a property of atp_platform, not of the build that made it.
target_compile_features(atp_platform INTERFACE cxx_std_23)

# The same argument for the source encoding, and it is what makes plugin_c.h's "every string crossing
# this boundary is UTF-8" true for someone else's build rather than only for ours: a plugin author who
# writes a non-ASCII literal would otherwise hand the host whatever their code page produced. A
# generator expression and not `if (MSVC)`, because this travels into the exported package and has to
# be decided by the consumer's compiler, not by ours — clang and gcc reject the spelling and need
# nothing. It does not reach a C_ABI plugin, which by design links nothing at all.
target_compile_options(atp_platform INTERFACE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

# The name the exported target has, made available in-tree as well, so documentation and an
# out-of-tree example can spell the dependency the same way a consumer of the package does.
add_library(atp::platform ALIAS atp_platform)
set_target_properties(atp_platform PROPERTIES EXPORT_NAME platform)

# The .h pattern is not redundant: plugin_c.h is a C header by necessity (it is included by plugins that
# have no C++ compiler), and without it the one file a foreign-language author reads is the one missing
# from the IDE's tree.
file(GLOB_RECURSE ATP_PLATFORM_HEADERS CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/include/atp/*.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/include/atp/*.h)

# The same file the installed package ships and includes, so the plugins in this tree are declared by
# the function an out-of-tree author calls rather than by a copy of what it does.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/AniToolsPlatformPluginHelpers.cmake)

if (ATP_INSTALL)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Install.cmake)
endif ()

add_subdirectory(src/runtime)

file(GLOB_RECURSE ATP_RUNTIME_HEADERS CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/include/atp/*.hpp)

add_subdirectory(examples/demo)
add_subdirectory(examples/plugin_demo)
add_subdirectory(examples/plugin_c_demo)
# Before src/app, which asks whether the bridge target exists in order to copy it next to atp_app.
if (ATP_BUILD_PYTHON_BRIDGE)
    add_subdirectory(src/bridges/python)
endif ()
if (ATP_BUILD_LUA_BRIDGE)
    add_subdirectory(src/bridges/lua)
endif ()
add_subdirectory(src/app)
# Both are added unconditionally, and the GUI option is honoured inside src/studio. The studio core
# and the MCP server are headless INTERFACE targets that never mention Qt, and atp_tests covers both,
# so putting them behind the GUI switch only made ATP_BUILD_STUDIO=OFF fail to build.
add_subdirectory(src/studio)
add_subdirectory(src/mcp)

find_package(Doxygen QUIET)
if (DOXYGEN_FOUND)
    add_custom_target(docs
            COMMAND Doxygen::doxygen ${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "doxygen: generating the API reference into build/doxygen/html"
            VERBATIM)
else ()
    message(STATUS "Doxygen not found — the 'docs' target is skipped")
endif ()

enable_testing()
add_subdirectory(tests)

# After the subdirectories, and that is a requirement: the target decides which sources it covers by
# asking which targets exist, and before this point none of them do.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Tidy.cmake)

# Last, and that is a requirement rather than tidiness: include(CPack) inside reads the CPACK_*
# variables and the install rules as they stand when it runs.
if (ATP_INSTALL)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Packaging.cmake)
endif ()
