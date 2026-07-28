# Auto-installs Qt through aqtinstall when Qt is not found and ATP_AUTO_INSTALL_QT is on. It
# downloads the official pre-built Qt binaries into external/ (gitignored) — the same third-party
# cache idea FetchContent uses for nlohmann_json and googletest. After the include() the caller's
# scope has Qt6_FOUND defined (and, on success, the imported Qt6::* targets): include() creates no
# new scope, so the find_package result is visible to the calling CMakeLists.

# The version is pinned and matches the developer's local Qt installation.
set(ATP_QT_VERSION 6.10.3)

# Step one is an ordinary lookup. A system Qt, or one from CMAKE_PREFIX_PATH, takes precedence and
# leaves aqt untouched.
find_package(Qt6 COMPONENTS Widgets QUIET)

if (NOT Qt6_FOUND AND ATP_AUTO_INSTALL_QT)
    # aqt's host/arch and the unpack directory differ per platform; Linux/GCC and Windows/MSVC are
    # supported.
    if (CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        set(_atp_qt_host linux)
        set(_atp_qt_arch linux_gcc_64)
        set(_atp_qt_dir gcc_64)
    elseif (CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
        set(_atp_qt_host windows)
        set(_atp_qt_arch win64_msvc2022_64)
        set(_atp_qt_dir msvc2022_64)
    else ()
        message(FATAL_ERROR
                "ATP_AUTO_INSTALL_QT: platform ${CMAKE_HOST_SYSTEM_NAME} is not supported")
    endif ()

    set(_atp_qt_root ${CMAKE_SOURCE_DIR}/external/qt)
    set(_atp_qt_prefix ${_atp_qt_root}/${ATP_QT_VERSION}/${_atp_qt_dir})
    set(_atp_venv ${CMAKE_SOURCE_DIR}/external/aqt-venv)

    # The venv interpreter lives in Scripts/ on Windows and in bin/ on POSIX. aqt is invoked as
    # `python -m aqt` to avoid depending on the console script name across operating systems.
    if (WIN32)
        set(_atp_venv_python ${_atp_venv}/Scripts/python.exe)
    else ()
        set(_atp_venv_python ${_atp_venv}/bin/python)
    endif ()

    # The venv with aqtinstall is created once. A stamp file marks the cache as ready rather than
    # the directory itself: a failed `python -m venv` (no ensurepip) still leaves the directory
    # behind, and checking for it would make the next configuration silently pick up a broken venv
    # and fail later on an obscure aqt error.
    set(_atp_venv_stamp ${_atp_venv}/.atp-aqt-ready)
    if (NOT EXISTS ${_atp_venv_stamp})
        find_package(Python3 COMPONENTS Interpreter REQUIRED)
        message(STATUS "ATP_AUTO_INSTALL_QT: creating a venv and installing aqtinstall into ${_atp_venv}")
        # Leftovers of a previous failed attempt: the venv is created from scratch, otherwise its
        # half-state could be inherited.
        file(REMOVE_RECURSE ${_atp_venv})
        execute_process(
                COMMAND ${Python3_EXECUTABLE} -m venv ${_atp_venv}
                RESULT_VARIABLE _atp_rc)
        if (NOT _atp_rc EQUAL 0)
            # On Debian/Ubuntu the venv module lives in a separate python3-venv package; without it
            # ensurepip is unavailable and creating a venv fails. Say so explicitly.
            message(FATAL_ERROR
                    "ATP_AUTO_INSTALL_QT: could not create a venv in ${_atp_venv}. "
                    "On Debian/Ubuntu install the venv package for your Python, for example: "
                    "apt install python3-venv (or python3.12-venv for a specific version).")
        endif ()
        execute_process(
                COMMAND ${_atp_venv_python} -m pip install --disable-pip-version-check aqtinstall
                RESULT_VARIABLE _atp_rc)
        if (NOT _atp_rc EQUAL 0)
            message(FATAL_ERROR "ATP_AUTO_INSTALL_QT: could not install aqtinstall")
        endif ()
        file(TOUCH ${_atp_venv_stamp})
    endif ()

    # Qt itself is downloaded once; the presence of the prefix directory marks the cache.
    if (NOT EXISTS ${_atp_qt_prefix})
        message(STATUS
                "ATP_AUTO_INSTALL_QT: downloading Qt ${ATP_QT_VERSION} (${_atp_qt_arch}) into ${_atp_qt_root}")
        execute_process(
                COMMAND ${_atp_venv_python} -m aqt install-qt
                        ${_atp_qt_host} desktop ${ATP_QT_VERSION} ${_atp_qt_arch}
                        --outputdir ${_atp_qt_root}
                RESULT_VARIABLE _atp_rc)
        if (NOT _atp_rc EQUAL 0)
            message(FATAL_ERROR "ATP_AUTO_INSTALL_QT: aqt install-qt failed")
        endif ()
    endif ()

    # A second lookup, now in the external Qt. REQUIRED rather than QUIET: once the user enabled the
    # auto-install and it succeeded, a missing Qt is an error, not a quiet skip.
    list(PREPEND CMAKE_PREFIX_PATH ${_atp_qt_prefix})
    find_package(Qt6 COMPONENTS Widgets REQUIRED)
endif ()
