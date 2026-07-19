# Логика studio — header-only библиотека: тесты подключают её напрямую,
# GUI-исполняемый файл придёт фазой 3.
add_library(atp_studio_lib INTERFACE)
target_include_directories(atp_studio_lib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(atp_studio_lib INTERFACE atp_app_lib)

file(GLOB_RECURSE ATP_STUDIO_HEADERS CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/atp/studio/*.hpp)
