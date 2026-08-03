# Benchmarks are opt-in (ATP_BUILD_BENCHMARKS, default OFF): a normal build and every CI job stay
# exactly as they were, and nobody pays the google/benchmark download for a build that will not run
# them.
include(FetchContent)

# benchmark's own tests pull googletest a second time and build a dozen targets nobody here runs.
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BENCHMARK_INSTALL_DOCS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
        benchmark
        URL https://github.com/google/benchmark/archive/refs/tags/v1.9.4.zip
        DOWNLOAD_DIR ${CMAKE_SOURCE_DIR}/external/downloads
        SOURCE_DIR ${CMAKE_SOURCE_DIR}/external/benchmark-src
        SYSTEM
)
FetchContent_MakeAvailable(benchmark)

file(GLOB ATP_BENCHMARK_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_executable(atp_benchmarks ${ATP_BENCHMARK_SOURCES})
target_link_libraries(atp_benchmarks PRIVATE atp_runtime atp_warnings benchmark::benchmark_main)
