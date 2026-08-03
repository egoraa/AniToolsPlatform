# Warning policy of the targets this repository owns, carried by an INTERFACE target rather than by
# CMAKE_CXX_FLAGS: a global flag variable reaches the vendored dependencies too, and a policy put on
# atp_platform would follow the SDK into a plugin author's build, where the choice is theirs.
# Everything links atp_warnings PRIVATE for the same reason.
#
# The headers are the code here — atp_platform, atp_runtime, atp_studio_lib and atp_mcp_lib are all
# INTERFACE targets — so a header is only ever diagnosed through a target that includes it, which is
# why every executable and every test target has to carry the policy for the warnings to be complete.

option(ATP_WERROR "Treat warnings as errors; CI turns this on, a local build should not" OFF)

add_library(atp_warnings INTERFACE)

# Chosen for what they catch in this codebase specifically:
#   -Wshadow          — the io registries bind reference members named exactly after their ports,
#                       and a constructor parameter of the same name would bind the wrong one.
#   -Wnon-virtual-dtor— module_base, io_base and their heirs are deleted through base pointers.
#   -Wold-style-cast  — the platform's cross-DLL machinery relies on precise cast kinds
#                       (reinterpret_cast for symbols, static_cast after an accepts() check).
#   -Wfloat-equal is deliberately absent: property<double> compares values by design.
set(ATP_WARNINGS_GNU
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-qual
        -Wdouble-promotion
        -Wformat=2)

# /permissive- is what makes MSVC agree with gcc and clang about two-phase lookup, which a
# header-only template library depends on: without it MSVC accepts code the other three reject.
set(ATP_WARNINGS_MSVC
        /W4
        /permissive-)

target_compile_options(atp_warnings INTERFACE
        $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:MSVC>>:${ATP_WARNINGS_MSVC}>
        $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:${ATP_WARNINGS_GNU}>)

if (ATP_WERROR)
    target_compile_options(atp_warnings INTERFACE
            $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:MSVC>>:/WX>
            $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:-Werror>)
endif ()
