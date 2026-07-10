# Демо-приложение платформы. Заголовки библиотеки указаны в сорцах цели —
# только для видимости в IDE (глоб определён в корневом CMakeLists.txt).
add_executable(atp_demo main.cpp ${ATP_PLATFORM_HEADERS})
target_link_libraries(atp_demo PRIVATE atp_platform)
