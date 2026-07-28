# Demo application of the platform. The library headers are listed among the target's sources for
# IDE visibility only; the globs are defined in the root CMakeLists.txt.
add_executable(atp_demo main.cpp ${ATP_PLATFORM_HEADERS} ${ATP_RUNTIME_HEADERS})
target_link_libraries(atp_demo PRIVATE atp_runtime)
