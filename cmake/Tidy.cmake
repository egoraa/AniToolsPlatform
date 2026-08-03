# The `tidy` target: run clang-tidy over every translation unit this repository compiles.
#
# It exists because the obvious way to run the analyser — a loop over the compilation database — takes
# forty minutes on a 32-core machine, and it takes them one core at a time. Measured on this project:
# ~25 seconds per translation unit whether one check is enabled or all hundred and fifty, because the
# cost is re-parsing a header-only library, not the checks. So the work is handed to the build system
# instead of to a loop: one command per unit, and `--parallel` does what it always does.
#
# Each unit gets a stamp file, which makes the run incremental — but the dependency is on every
# header, not just the unit's own source. In a header-only library a change to one header reaches
# almost every unit, and a stamp that ignored that would report a clean tree that was never analysed.
# That is the same reason the CI job analyses everything rather than the changed files.
#
# The version is pinned by CI at major 18 and deliberately not by this file: a newer clang-tidy is
# useful locally — it finds more — as long as the developer knows that what CI enforces is 18. A
# different major is therefore reported, never refused.
#
# The binary has to be findable. It ships inside CLion and is not on PATH, which is the same trap
# clang-format had, so either install one:
#
#     pip install clang-tidy
#
# or point the cache variable at the one already on the machine:
#
#     cmake -S . -B <dir> -DATP_CLANG_TIDY=<path to clang-tidy>
set(ATP_CLANG_TIDY_MAJOR 18)

find_program(ATP_CLANG_TIDY NAMES clang-tidy)

if (NOT ATP_CLANG_TIDY)
    message(STATUS "clang-tidy not found — the 'tidy' target is skipped")
    return()
endif ()

if (NOT CMAKE_EXPORT_COMPILE_COMMANDS)
    message(STATUS "CMAKE_EXPORT_COMPILE_COMMANDS is off — the 'tidy' target is skipped "
            "(the Visual Studio generator cannot produce a compilation database; configure a Ninja "
            "build directory for the analyser)")
    return()
endif ()

execute_process(COMMAND ${ATP_CLANG_TIDY} --version
        OUTPUT_VARIABLE _atp_ct_version OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" _atp_ct_found "${_atp_ct_version}")
string(REGEX REPLACE "\\..*" "" _atp_ct_found_major "${_atp_ct_found}")

if (NOT _atp_ct_found_major STREQUAL ATP_CLANG_TIDY_MAJOR)
    message(STATUS
            "clang-tidy is ${_atp_ct_found}, while CI enforces major ${ATP_CLANG_TIDY_MAJOR} — "
            "a finding here may not be a finding there, and the other way round")
endif ()

# Only the directories whose sources this build actually compiles: templates/ is a separate project
# reached through find_package, so its units are in nobody's compilation database.
file(GLOB_RECURSE _atp_tidy_sources CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/*.cpp)

if (NOT TARGET atp_studio_ui)
    list(FILTER _atp_tidy_sources EXCLUDE REGEX "/src/studio/ui/")
endif ()
if (NOT TARGET atp_ui_tests)
    list(FILTER _atp_tidy_sources EXCLUDE REGEX "/tests/ui/")
endif ()
if (NOT ATP_BUILD_BENCHMARKS)
    list(FILTER _atp_tidy_sources EXCLUDE REGEX "/tests/benchmarks/")
endif ()

file(GLOB_RECURSE _atp_tidy_headers CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.hpp)

set(_atp_tidy_stamps "")
foreach (_atp_tidy_source IN LISTS _atp_tidy_sources)
    file(RELATIVE_PATH _atp_tidy_rel ${CMAKE_CURRENT_SOURCE_DIR} ${_atp_tidy_source})
    string(REPLACE "/" "_" _atp_tidy_key ${_atp_tidy_rel})
    set(_atp_tidy_stamp ${CMAKE_BINARY_DIR}/tidy/${_atp_tidy_key}.stamp)
    add_custom_command(OUTPUT ${_atp_tidy_stamp}
            COMMAND ${ATP_CLANG_TIDY} -p ${CMAKE_BINARY_DIR} --quiet ${_atp_tidy_source}
            COMMAND ${CMAKE_COMMAND} -E touch ${_atp_tidy_stamp}
            DEPENDS ${_atp_tidy_source} ${_atp_tidy_headers} ${CMAKE_CURRENT_SOURCE_DIR}/.clang-tidy
            COMMENT "clang-tidy: ${_atp_tidy_rel}"
            VERBATIM)
    list(APPEND _atp_tidy_stamps ${_atp_tidy_stamp})
endforeach ()

add_custom_target(tidy DEPENDS ${_atp_tidy_stamps})

list(LENGTH _atp_tidy_sources _atp_tidy_count)
message(STATUS "clang-tidy ${_atp_ct_found}: the 'tidy' target covers ${_atp_tidy_count} translation units")
