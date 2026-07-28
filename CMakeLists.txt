cmake_minimum_required(VERSION 4.1)
project(AniToolsPlatform)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# If Qt is not found, download it via aqtinstall into external/ (see cmake/AutoInstallQt.cmake).
# OFF by default: configurations with a system Qt or a Qt from CMAKE_PREFIX_PATH stay untouched.
option(ATP_AUTO_INSTALL_QT "Download Qt via aqtinstall into external/ if not found" OFF)

# Studio is the Qt GUI host. It is switched off in environments without Qt (CI, headless builds), so
# that the rest of the platform builds without a Qt dependency.
option(ATP_BUILD_STUDIO "Build the studio GUI application" ON)

# The platform headers: one include point for the application and the tests.
add_library(atp_platform INTERFACE)
target_include_directories(atp_platform INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# The headers are globbed for IDE visibility only; CONFIGURE_DEPENDS re-runs the configuration when
# new files appear.
file(GLOB_RECURSE ATP_PLATFORM_HEADERS CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/include/atp/*.hpp)

# Host runtime: pipeline execution, plugin and config loading. A separate target, so that the module
# author's SDK (atp_platform) does not drag the host machinery in: plugins see include/ only, host
# applications see include/ plus src/runtime.
add_subdirectory(src/runtime)

# The glob lives here rather than in src/runtime, because examples/demo consumes the variable and a
# subdirectory's variables do not reach sibling directories.
file(GLOB_RECURSE ATP_RUNTIME_HEADERS CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/include/atp/*.hpp)

add_subdirectory(examples/demo)
add_subdirectory(examples/plugin_demo)
add_subdirectory(src/app)
# The MCP server rides on the studio's headless core, so it shares the studio's switch: without
# atp_studio_lib there is nothing for it to expose.
if (ATP_BUILD_STUDIO)
    add_subdirectory(src/studio)
    add_subdirectory(src/mcp)
endif ()

# API reference. The target is optional and never part of ALL: doxygen is a documentation tool, not
# a build dependency, so a machine without it configures and builds exactly as before. Doxygen
# resolves the relative paths of its config against the working directory, which is why the target
# runs from the project root — the same way `doxygen docs/Doxyfile` is run by hand.
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
