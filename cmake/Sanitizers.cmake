# Runtime sanitizers, opt-in and off by default.
#
# Why this matters more here than in most projects: nearly every load-bearing rule of the platform is
# a threading contract asserted by construction and not by a test. io_base::lock() implements the
# `unsafe` tag as a std::unique_lock built with std::defer_lock — a guard that locks nothing;
# input_base::set_notifier is documented as setup-phase-only because concurrent use with delivery is
# a race; the stop/wait race on pipeline_runner is "excluded by contract rather than by
# synchronisation"; and delivery deliberately runs outside the output's lock so that the two mutexes
# never nest. A thread sanitizer run over tests/runtime/pipeline_runner_tests.cpp, which starts real
# threads, checks all of that at once and is the only thing that can.
#
# Unlike the warning policy, this is applied with directory-wide add_compile_options rather than an
# INTERFACE target, and deliberately: a sanitizer wants the *whole* program instrumented. googletest
# is a compiled dependency that hands std::string and std::vector across the boundary, and mixing
# instrumented and uninstrumented code there produces ASan container-overflow false positives and
# blinds TSan to the synchronisation it cannot see. Being included from the top level before any
# add_subdirectory, this covers the FetchContent subprojects too. It still does not follow
# atp_platform into a plugin author's build, which is what the INTERFACE target was protecting
# against for warnings.
#
# The value is a sanitizer name as the compiler spells it, so a combination is written the way clang
# and gcc take it: -DATP_SANITIZER=address,undefined.
set(ATP_SANITIZER "OFF" CACHE STRING
        "Runtime sanitizer: OFF, or a -fsanitize= value such as address, thread, undefined, address,undefined")
set_property(CACHE ATP_SANITIZER PROPERTY STRINGS OFF address thread undefined address,undefined)

if (NOT ATP_SANITIZER STREQUAL "OFF")
    if (MSVC)
        # MSVC ships AddressSanitizer only — there is no /fsanitize=thread — so the sanitizer that
        # matters most for this codebase is reachable on Linux alone. Say so rather than emitting
        # flags the compiler will reject with a less obvious message.
        if (NOT ATP_SANITIZER STREQUAL "address")
            message(FATAL_ERROR
                    "ATP_SANITIZER=${ATP_SANITIZER} is not available on MSVC, which implements "
                    "AddressSanitizer only. Use ATP_SANITIZER=address here, or build with clang/gcc.")
        endif ()

        # /RTC1 is what CMake puts in a Debug build by default and it is incompatible with
        # /fsanitize=address; the compiler rejects the pair outright. Incremental linking is
        # unsupported with ASan as well, and the default Debug link is incremental. This file is
        # include()d from the top level, which is not a new scope, so these normal variables shadow
        # the cache entries for every subdirectory below.
        foreach (flags_var IN ITEMS CMAKE_C_FLAGS_DEBUG CMAKE_CXX_FLAGS_DEBUG)
            string(REGEX REPLACE "/RTC[1csu]+" "" ${flags_var} "${${flags_var}}")
        endforeach ()

        add_compile_options(/fsanitize=address)
        add_link_options(/INCREMENTAL:NO)

        # An ASan-instrumented binary loads clang_rt.asan_dynamic at startup, and the toolchain does
        # not put it anywhere the loader looks — without this the test executable dies before main
        # with "cannot open shared object file". The DLL lives next to cl.exe, which is the one place
        # its location can be derived from rather than guessed.
        get_filename_component(_atp_msvc_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
        find_file(ATP_ASAN_RUNTIME clang_rt.asan_dynamic-x86_64.dll
                HINTS "${_atp_msvc_bin}" NO_DEFAULT_PATH)
        if (NOT ATP_ASAN_RUNTIME)
            message(WARNING
                    "ATP_SANITIZER=address: clang_rt.asan_dynamic-x86_64.dll was not found next to "
                    "${CMAKE_CXX_COMPILER}. The binaries will build but will not start until it is "
                    "on PATH.")
        endif ()
    else ()
        # -fno-sanitize-recover makes a finding fatal where it happens: UBSan otherwise prints and
        # carries on, which in CI means a green job with diagnostics nobody reads.
        add_compile_options(-fsanitize=${ATP_SANITIZER} -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=${ATP_SANITIZER})
    endif ()

    message(STATUS "Sanitizer enabled: ${ATP_SANITIZER}")
endif ()
