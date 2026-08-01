add_executable(atp_demo main.cpp ${ATP_PLATFORM_HEADERS} ${ATP_RUNTIME_HEADERS})
target_link_libraries(atp_demo PRIVATE atp_runtime)
