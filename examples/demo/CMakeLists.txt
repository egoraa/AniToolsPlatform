# Демо-приложение платформы. Заголовки библиотеки указаны в сорцах цели —
# только для видимости в IDE (глобы определены в корневом CMakeLists.txt).
add_executable(atp_demo main.cpp ${ATP_PLATFORM_HEADERS} ${ATP_HOST_HEADERS})
target_link_libraries(atp_demo PRIVATE atp_host)
