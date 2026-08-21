add_library(atp_studio_lib INTERFACE)
target_include_directories(atp_studio_lib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
# nlohmann arrives here rather than through atp_runtime because studio is an MCP client: remote_client,
# remote_runtime, project_from_description, settings and position_file all speak JSON, while the project
# document itself no longer does. The runtime deliberately hands out none of it — a document library is
# private to atp_json — so a target that speaks the protocol asks for one by name.
target_link_libraries(atp_studio_lib INTERFACE atp_runtime nlohmann_json::nlohmann_json)

file(GLOB_RECURSE ATP_STUDIO_HEADERS CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/include/atp/studio/*.hpp)

# Everything above is the headless core and is always built. Only what follows is the GUI, so the
# option is asked here rather than around the whole directory — and Qt is not even looked for when the
# answer is no, which is the point of switching it off.
if (ATP_BUILD_STUDIO)
    include(${CMAKE_SOURCE_DIR}/cmake/AutoInstallQt.cmake)
    if (Qt6_FOUND)
        add_subdirectory(ui)
    else ()
        message(STATUS "Qt6 not found — atp_studio is skipped")
    endif ()
else ()
    message(STATUS "ATP_BUILD_STUDIO=OFF — atp_studio is skipped, the studio core is not")
endif ()
