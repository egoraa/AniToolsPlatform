add_library(atp_studio_lib INTERFACE)
target_include_directories(atp_studio_lib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(atp_studio_lib INTERFACE atp_runtime)

file(GLOB_RECURSE ATP_STUDIO_HEADERS CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/include/atp/studio/*.hpp)

include(${CMAKE_SOURCE_DIR}/cmake/AutoInstallQt.cmake)
if (Qt6_FOUND)
    add_subdirectory(ui)
else ()
    message(STATUS "Qt6 not found — atp_studio is skipped")
endif ()
