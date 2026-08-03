# Script half of the `format` target (see Format.cmake). It runs at build time rather than at
# configure time, so the file list is the one git reports now and not the one it reported when the
# build directory was created.
execute_process(
        COMMAND git ls-files include/*.hpp src/*.hpp src/*.cpp tests/*.hpp tests/*.cpp examples/*.hpp examples/*.cpp
                templates/*.hpp templates/*.cpp
        WORKING_DIRECTORY ${ATP_SOURCE_DIR}
        OUTPUT_VARIABLE atp_sources
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE atp_rc)

if (NOT atp_rc EQUAL 0)
    message(FATAL_ERROR "format: 'git ls-files' failed — the target needs a git checkout")
endif ()

string(REPLACE "\n" ";" atp_sources "${atp_sources}")
list(LENGTH atp_sources atp_count)

execute_process(
        COMMAND ${ATP_CLANG_FORMAT} -i ${atp_sources}
        WORKING_DIRECTORY ${ATP_SOURCE_DIR}
        RESULT_VARIABLE atp_rc)

if (NOT atp_rc EQUAL 0)
    message(FATAL_ERROR "format: clang-format failed")
endif ()

message(STATUS "format: ${atp_count} files")
