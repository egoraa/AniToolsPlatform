# Авто-установка Qt через aqtinstall, когда Qt не найден и включён ATP_AUTO_INSTALL_QT.
# Скачивает официальные прекомпилированные бинарники Qt в external/ (gitignored) —
# та же идея кэша сторонних исходников, что FetchContent для nlohmann_json/googletest.
# После include() в вызывающей области видимости определён Qt6_FOUND (и, при успехе,
# импортированные цели Qt6::*): include() не создаёт новую область, поэтому результат
# find_package виден вызывающему CMakeLists.

# Версия фиксирована и совпадает с путём Windows-пресета в CMakePresets.json.
set(ATP_QT_VERSION 6.10.3)

# Шаг 1: обычный поиск. Системный Qt или Qt из CMAKE_PREFIX_PATH пресета имеет
# приоритет — aqt в этом случае не трогаем вовсе.
find_package(Qt6 COMPONENTS Widgets QUIET)

if (NOT Qt6_FOUND AND ATP_AUTO_INSTALL_QT)
    # host/arch aqt и каталог распаковки различаются по платформам; поддерживаем те же,
    # что описаны в CMakePresets.json (Linux/GCC и Windows/MSVC).
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
                "ATP_AUTO_INSTALL_QT: платформа ${CMAKE_HOST_SYSTEM_NAME} не поддерживается")
    endif ()

    set(_atp_qt_root ${CMAKE_SOURCE_DIR}/external/qt)
    set(_atp_qt_prefix ${_atp_qt_root}/${ATP_QT_VERSION}/${_atp_qt_dir})
    set(_atp_venv ${CMAKE_SOURCE_DIR}/external/aqt-venv)

    # Интерпретатор venv: на Windows он в Scripts/, на POSIX — в bin/. aqt зовём как
    # `python -m aqt`, чтобы не зависеть от имени консольного скрипта в разных ОС.
    if (WIN32)
        set(_atp_venv_python ${_atp_venv}/Scripts/python.exe)
    else ()
        set(_atp_venv_python ${_atp_venv}/bin/python)
    endif ()

    # venv с aqtinstall создаём один раз. Признак готового кэша — стамп-файл, а не сам
    # каталог: неудачный `python -m venv` (нет ensurepip) всё равно оставляет каталог,
    # и проверка по нему заставила бы следующую конфигурацию молча взять битый venv
    # и упасть позже на невнятной ошибке aqt.
    set(_atp_venv_stamp ${_atp_venv}/.atp-aqt-ready)
    if (NOT EXISTS ${_atp_venv_stamp})
        find_package(Python3 COMPONENTS Interpreter REQUIRED)
        message(STATUS "ATP_AUTO_INSTALL_QT: создаю venv и ставлю aqtinstall в ${_atp_venv}")
        # Остатки прошлой неудачной попытки: venv создаётся с нуля, иначе можно
        # унаследовать её полусостояние.
        file(REMOVE_RECURSE ${_atp_venv})
        execute_process(
                COMMAND ${Python3_EXECUTABLE} -m venv ${_atp_venv}
                RESULT_VARIABLE _atp_rc)
        if (NOT _atp_rc EQUAL 0)
            # На Debian/Ubuntu модуль venv вынесен в отдельный пакет python3-venv;
            # без него ensurepip недоступен и создание venv падает. Подсказываем явно.
            message(FATAL_ERROR
                    "ATP_AUTO_INSTALL_QT: не удалось создать venv в ${_atp_venv}. "
                    "На Debian/Ubuntu установите пакет venv для вашего Python, например: "
                    "apt install python3-venv (или python3.12-venv под конкретную версию).")
        endif ()
        execute_process(
                COMMAND ${_atp_venv_python} -m pip install --disable-pip-version-check aqtinstall
                RESULT_VARIABLE _atp_rc)
        if (NOT _atp_rc EQUAL 0)
            message(FATAL_ERROR "ATP_AUTO_INSTALL_QT: не удалось установить aqtinstall")
        endif ()
        file(TOUCH ${_atp_venv_stamp})
    endif ()

    # Собственно Qt скачиваем один раз; наличие prefix-каталога — признак кэша.
    if (NOT EXISTS ${_atp_qt_prefix})
        message(STATUS
                "ATP_AUTO_INSTALL_QT: скачиваю Qt ${ATP_QT_VERSION} (${_atp_qt_arch}) в ${_atp_qt_root}")
        execute_process(
                COMMAND ${_atp_venv_python} -m aqt install-qt
                        ${_atp_qt_host} desktop ${ATP_QT_VERSION} ${_atp_qt_arch}
                        --outputdir ${_atp_qt_root}
                RESULT_VARIABLE _atp_rc)
        if (NOT _atp_rc EQUAL 0)
            message(FATAL_ERROR "ATP_AUTO_INSTALL_QT: aqt install-qt завершился с ошибкой")
        endif ()
    endif ()

    # Повторный поиск уже во внешнем Qt. REQUIRED, а не QUIET: раз пользователь включил
    # автоустановку и она отработала, ненайденный Qt — это ошибка, а не тихий пропуск.
    list(PREPEND CMAKE_PREFIX_PATH ${_atp_qt_prefix})
    find_package(Qt6 COMPONENTS Widgets REQUIRED)
endif ()
