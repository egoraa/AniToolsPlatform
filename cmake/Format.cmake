# The `format` target: reformat every source this repository owns, in place.
#
# The version is pinned, and not out of pedantry. clang-format's output moves between releases, so an
# unpinned tool reformats files it has no business touching and turns a green branch red for reasons
# that have nothing to do with the change under review. The CI job installs exactly
# ATP_CLANG_FORMAT_VERSION through pip; the same command gives a developer the identical binary on any
# platform, which also solves the older problem that clang-format ships inside CLion and is not on
# PATH:
#
#     pip install clang-format==22.1.8
#
# A different major is not refused — it is reported, so that a surprising diff has a visible cause.
set(ATP_CLANG_FORMAT_VERSION 22.1.8)

find_program(ATP_CLANG_FORMAT NAMES clang-format)

if (ATP_CLANG_FORMAT)
    execute_process(COMMAND ${ATP_CLANG_FORMAT} --version
            OUTPUT_VARIABLE _atp_cf_version OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    # Both majors are captured into named variables rather than read out of CMAKE_MATCH_1: the next
    # regex call overwrites that one, and comparing it against a later match silently compares
    # nothing.
    string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" _atp_cf_found "${_atp_cf_version}")
    string(REGEX REPLACE "\\..*" "" _atp_cf_found_major "${_atp_cf_found}")
    string(REGEX REPLACE "\\..*" "" _atp_cf_want_major "${ATP_CLANG_FORMAT_VERSION}")

    if (NOT _atp_cf_found_major STREQUAL _atp_cf_want_major)
        message(STATUS
                "clang-format is ${_atp_cf_version}, while CI enforces ${ATP_CLANG_FORMAT_VERSION} — "
                "the 'format' target may produce a diff the format job then rejects")
    endif ()

    # The file list is git's, so a generated or vendored source cannot wander into it.
    add_custom_target(format
            COMMAND ${CMAKE_COMMAND}
                    -DATP_CLANG_FORMAT=${ATP_CLANG_FORMAT}
                    -DATP_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunFormat.cmake
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "clang-format: reformatting the sources in place"
            VERBATIM)
else ()
    message(STATUS "clang-format not found — the 'format' target is skipped")
endif ()
