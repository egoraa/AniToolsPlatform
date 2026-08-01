set(ATP_QT_VERSION 6.10.3)

set(ATP_QT_MINIMUM 6.8)

find_package(Qt6 ${ATP_QT_MINIMUM} COMPONENTS Widgets QUIET)

if (NOT Qt6_FOUND AND ATP_AUTO_INSTALL_QT)
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

    if (WIN32)
        set(_atp_venv_python ${_atp_venv}/Scripts/python.exe)
    else ()
        set(_atp_venv_python ${_atp_venv}/bin/python)
    endif ()

    set(_atp_venv_stamp ${_atp_venv}/.atp-aqt-ready)
    if (NOT EXISTS ${_atp_venv_stamp})
        find_package(Python3 COMPONENTS Interpreter REQUIRED)
        message(STATUS "ATP_AUTO_INSTALL_QT: creating a venv and installing aqtinstall into ${_atp_venv}")
        file(REMOVE_RECURSE ${_atp_venv})
        execute_process(
                COMMAND ${Python3_EXECUTABLE} -m venv ${_atp_venv}
                RESULT_VARIABLE _atp_rc)
        if (NOT _atp_rc EQUAL 0)
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

    list(PREPEND CMAKE_PREFIX_PATH ${_atp_qt_prefix})
    find_package(Qt6 ${ATP_QT_MINIMUM} COMPONENTS Widgets REQUIRED)
endif ()
