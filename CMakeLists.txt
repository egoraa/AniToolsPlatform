cmake_minimum_required(VERSION 4.1)
project(AniToolsPlatform VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ATP_AUTO_INSTALL_QT "Download Qt via aqtinstall into external/ if not found" OFF)

option(ATP_BUILD_STUDIO "Build the studio GUI application" ON)

add_library(atp_platform INTERFACE)
target_include_directories(atp_platform INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

file(GLOB_RECURSE ATP_PLATFORM_HEADERS CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/include/atp/*.hpp)

add_subdirectory(src/runtime)

file(GLOB_RECURSE ATP_RUNTIME_HEADERS CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/include/atp/*.hpp)

add_subdirectory(examples/demo)
add_subdirectory(examples/plugin_demo)
add_subdirectory(src/app)
if (ATP_BUILD_STUDIO)
    add_subdirectory(src/studio)
    add_subdirectory(src/mcp)
endif ()

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
