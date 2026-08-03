# Declares a plugin of the platform. Shipped inside the installed package and included by this
# repository's own build, so the in-tree plugins exercise the very function an out-of-tree author
# calls and it cannot quietly rot.
#
#     atp_add_plugin(my_plugin SOURCES plugin.cpp [OUTPUT_NAME my_plugin])
#
# Every property it sets is load-bearing, which is the reason for having the function at all: written
# out by hand they look like boilerplate, and an author who drops one gets a plugin that either fails
# to load or — worse — loads and then silently refuses connections.
function(atp_add_plugin name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "OUTPUT_NAME" "SOURCES")

    if (arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "atp_add_plugin(${name}): unexpected arguments '${arg_UNPARSED_ARGUMENTS}'")
    endif ()
    if (NOT arg_SOURCES)
        message(FATAL_ERROR "atp_add_plugin(${name}): SOURCES is required and must not be empty")
    endif ()
    if (NOT arg_OUTPUT_NAME)
        set(arg_OUTPUT_NAME ${name})
    endif ()

    # MODULE and not SHARED, because CMake refuses to let another target link a MODULE library. A
    # plugin is opened by path at run time and linking one is a mistake; this makes that mistake a
    # configure error instead of a convention. It does not change what the linker emits — MSVC still
    # writes an import library, since the C handshake symbols are __declspec(dllexport).
    add_library(${name} MODULE ${arg_SOURCES})

    # atp::platform alone. Linking the host runtime would put a second copy of the registries and the
    # loader into the process, and the two copies would disagree about which modules exist.
    target_link_libraries(${name} PRIVATE atp::platform)

    set_target_properties(${name} PROPERTIES
            # Hidden visibility is what forces type identity across the plugin boundary to be decided
            # by name (include/atp/type_compare.hpp) rather than by the address of a per-library
            # static — the discipline the io layer is built on.
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN ON
            # One file name on every toolchain: no "lib" prefix, an explicit output name. A config may
            # write a plugin path without an extension, and atp::module_loader appends the platform
            # one, so the name is part of the format rather than a matter of taste.
            PREFIX ""
            OUTPUT_NAME "${arg_OUTPUT_NAME}")

    # CMake gives a MODULE library the .so suffix on Apple, where atp::plugin_extension says .dylib.
    # Left alone, the file would still load by an explicit path while the extensionless config form
    # and the scan of a plugin directory would both stop finding it — on that platform only.
    if (APPLE)
        set_target_properties(${name} PROPERTIES SUFFIX ".dylib")
    endif ()

    # The one incompatibility the ABI handshake cannot see. A plugin on the static CRT gets its own
    # heap and its own copy of the standard library's statics, so an allocation crossing the boundary
    # — every std::string in a port name — is undefined. The default is already the DLL runtime; this
    # only fires when a project has deliberately asked for the other one.
    if (MSVC AND CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "^MultiThreaded(Debug)?$")
        message(WARNING
                "atp_add_plugin(${name}): CMAKE_MSVC_RUNTIME_LIBRARY is '${CMAKE_MSVC_RUNTIME_LIBRARY}', "
                "a static CRT. Host and plugin must share one C++ runtime; this plugin will misbehave "
                "in ways the atp_abi_version() handshake cannot detect.")
    endif ()
endfunction()
