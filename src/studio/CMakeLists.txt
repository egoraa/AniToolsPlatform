# Studio's logic is a header-only library, which the tests link directly.
add_library(atp_studio_lib INTERFACE)
target_include_directories(atp_studio_lib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(atp_studio_lib INTERFACE atp_runtime)

# Headers are globbed for IDE visibility only, as in the root CMakeLists.
file(GLOB_RECURSE ATP_STUDIO_HEADERS CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/include/atp/studio/*.hpp)

# The GUI is built only when Qt6 is found, so a configuration without Qt (say, Linux before
# qt6-base-dev is installed) stays workable. The module looks Qt up and, with ATP_AUTO_INSTALL_QT,
# installs it via aqtinstall; after the include() Qt6_FOUND is defined.
include(${CMAKE_SOURCE_DIR}/cmake/AutoInstallQt.cmake)
if (Qt6_FOUND)
    add_subdirectory(ui)
else ()
    message(STATUS "Qt6 not found — atp_studio is skipped")
endif ()
