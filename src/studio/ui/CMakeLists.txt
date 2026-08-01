qt_standard_project_setup()

find_package(Qt6 COMPONENTS Svg REQUIRED)

file(GLOB_RECURSE ATP_STUDIO_UI_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp)
list(REMOVE_ITEM ATP_STUDIO_UI_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)

qt_add_library(atp_studio_ui STATIC ${ATP_STUDIO_UI_SOURCES})
target_include_directories(atp_studio_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(atp_studio_ui PUBLIC atp_studio_lib Qt6::Widgets Qt6::Svg ${CMAKE_DL_LIBS})

qt_add_executable(atp_studio main.cpp)
target_link_libraries(atp_studio PRIVATE atp_studio_ui)

target_compile_definitions(atp_studio_ui PRIVATE ATP_STUDIO_VERSION="${PROJECT_VERSION}")

set_source_files_properties(resources/atp.ico PROPERTIES QT_RESOURCE_ALIAS atp.ico)
qt_add_resources(atp_studio "brand" PREFIX "/" FILES resources/atp.ico)
if (WIN32)
    target_sources(atp_studio PRIVATE resources/atp_studio.rc)
endif ()

file(GLOB ATP_STUDIO_ICONS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/*.svg)
foreach (icon IN LISTS ATP_STUDIO_ICONS)
    cmake_path(GET icon FILENAME icon_name)
    set_source_files_properties(${icon} PROPERTIES QT_RESOURCE_ALIAS icons/${icon_name})
endforeach ()
qt_add_resources(atp_studio "icons" PREFIX "/" FILES ${ATP_STUDIO_ICONS})

if (WIN32)
    find_program(ATP_WINDEPLOYQT windeployqt HINTS "${Qt6_DIR}/../../../bin")
    if (ATP_WINDEPLOYQT)
        add_custom_command(TARGET atp_studio POST_BUILD
                COMMAND "${ATP_WINDEPLOYQT}" --no-translations --no-compiler-runtime
                        "$<TARGET_FILE:atp_studio>"
                COMMENT "windeployqt: Qt runtime next to atp_studio")
    endif ()
endif ()
