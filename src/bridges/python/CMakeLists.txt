# Development.Embed and not Development.Module: the bridge embeds an interpreter into a host that is
# not python, so it needs the library itself and not just the headers an extension module gets. The
# floor is 3.11 because the buffer protocol entered the stable ABI there — without it a blob port
# could not accept a memoryview or an ndarray without making numpy a dependency of the bridge.
find_package(Python3 3.11 COMPONENTS Development.Embed QUIET)

if (NOT Python3_FOUND)
    message(STATUS "CPython 3.11+ development files not found — the Python bridge is skipped")
    return()
endif ()

atp_add_plugin(atp_python_bridge C_ABI SOURCES
        plugin.cpp
        ctx_type.cpp
        descriptors.cpp
        errors.cpp
        instance.cpp
        interpreter.cpp
        self_module.cpp
        values.cpp)
target_link_libraries(atp_python_bridge PRIVATE atp_warnings ${CMAKE_DL_LIBS})
target_compile_features(atp_python_bridge PRIVATE cxx_std_23)
# SYSTEM, because CI builds every job with ATP_WERROR=ON and the CPython headers are not ours to fix:
# a diagnostic that some compiler version decides to emit inside Python.h would fail a branch for a
# reason nobody in this repository can act on.
target_include_directories(atp_python_bridge SYSTEM PRIVATE ${Python3_INCLUDE_DIRS})
target_compile_definitions(atp_python_bridge PRIVATE Py_LIMITED_API=0x030B0000)

# Linking the version-independent import library is what makes Py_LIMITED_API worth anything: built
# against python3.lib this file keeps loading under a later CPython, while python313.lib would pin it
# to one minor version and undo the whole point. The versioned library stays as a fallback for
# installations that ship no other, and the message says which happened — the difference is invisible
# until the day somebody upgrades Python.
find_library(ATP_PYTHON_SABI_LIBRARY NAMES python3
        HINTS ${Python3_LIBRARY_DIRS} ${Python3_RUNTIME_LIBRARY_DIRS}
        NO_DEFAULT_PATH)
if (ATP_PYTHON_SABI_LIBRARY)
    target_link_libraries(atp_python_bridge PRIVATE ${ATP_PYTHON_SABI_LIBRARY})
    message(STATUS "Python bridge: stable ABI against ${ATP_PYTHON_SABI_LIBRARY}")
else ()
    target_link_libraries(atp_python_bridge PRIVATE Python3::Python)
    message(STATUS "Python bridge: no version-independent library found, pinned to Python ${Python3_VERSION}")
endif ()

# The package has to sit where the bridge looks for it — python/atp next to the library — in the build
# tree as much as in an installation, or only an unpacked release would ever run a script.
#
# A node of the build graph rather than a POST_BUILD step, and keyed on the package sources: a
# POST_BUILD command runs only when its target is relinked, so editing the package alone left the copy
# beside the library stale with nothing to say so. The bridge then loaded a package older than its
# source, which a test cannot tell from the change simply not working. The output is a stamp because
# what the command produces is a directory and OUTPUT admits no target-dependent generator expression
# either; it carries $<CONFIG> because a multi-config generator gives every configuration its own
# directory to copy into, and one shared stamp would leave the second configuration with no package.
file(GLOB ATP_PYTHON_PACKAGE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/package/atp/*.py)
set(ATP_PYTHON_PACKAGE_STAMP ${CMAKE_CURRENT_BINARY_DIR}/atp_python_package_$<CONFIG>.stamp)
add_custom_command(OUTPUT ${ATP_PYTHON_PACKAGE_STAMP}
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/package/atp
                $<TARGET_FILE_DIR:atp_python_bridge>/python/atp
        COMMAND ${CMAKE_COMMAND} -E touch ${ATP_PYTHON_PACKAGE_STAMP}
        DEPENDS ${ATP_PYTHON_PACKAGE_SOURCES}
        COMMENT "atp package next to atp_python_bridge")
add_custom_target(atp_python_package DEPENDS ${ATP_PYTHON_PACKAGE_STAMP})
add_dependencies(atp_python_bridge atp_python_package)

if (ATP_INSTALL)
    # The bridge is a plugin and installs like one, beside the others, with the package following it
    # into python/atp — the same "next to the library" rule as above, only in the install tree. Both
    # rules live where the target is declared; the demo script the sample pipeline names is an asset of
    # that pipeline and is installed by src/app instead.
    install(TARGETS atp_python_bridge
            LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}/${ATP_PLUGIN_DIRNAME} COMPONENT applications)
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/package/atp
            DESTINATION ${CMAKE_INSTALL_BINDIR}/${ATP_PLUGIN_DIRNAME}/python COMPONENT applications)
endif ()
