# Install and export rules for the module author SDK, so a plugin can be built against
# find_package(AniToolsPlatform) instead of inside this tree.
#
# The package ships **atp_platform alone**. atp_runtime is deliberately left unexported, for two
# separate reasons that happen to agree: a plugin linking the host runtime is a mistake — it would
# carry a second copy of the registries and the loader into the process — and atp_runtime pulls in
# nlohmann/json, which arrives here through FetchContent and therefore installs nothing of its own.
# Exporting it would mean either shipping a vendored third-party header set under this package's
# prefix or handing the consumer a package that cannot be configured until they produce a matching
# nlohmann_json themselves. A host that needs the runtime builds this repository; that is a real
# limitation and it is written down rather than papered over.

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# The ABI number stays in C++ — plugin.hpp is where it is documented, bumped and read by the
# handshake — and CMake reads it from there, so the package constant cannot drift from the header.
# Exactly one line may match: a second one would mean the header no longer has a single answer, and
# guessing which is right is worse than stopping.
set(_atp_plugin_header ${CMAKE_CURRENT_SOURCE_DIR}/include/atp/plugin.hpp)
file(STRINGS ${_atp_plugin_header} _atp_abi_lines REGEX "^inline constexpr unsigned plugin_abi = [0-9]+;")
list(LENGTH _atp_abi_lines _atp_abi_count)
if (NOT _atp_abi_count EQUAL 1)
    message(FATAL_ERROR
            "could not read plugin_abi from ${_atp_plugin_header}: expected exactly one definition, "
            "found ${_atp_abi_count}. cmake/Install.cmake exports that number as a package constant "
            "and must not guess it.")
endif ()
string(REGEX MATCH "[0-9]+" ATP_PLUGIN_ABI "${_atp_abi_lines}")

# Everything here is the `sdk` component, so that `cmake --install --component sdk` can ask for the
# headers and the package without the applications. A consumer that wants only the SDK — the CI job
# that builds a plugin out of tree, for one — would otherwise have to build every executable in the
# tree just to run the install step, and would break again the next time one is added.
install(TARGETS atp_platform EXPORT AniToolsPlatformTargets COMPONENT sdk)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/atp
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        COMPONENT sdk)

install(EXPORT AniToolsPlatformTargets
        FILE AniToolsPlatformTargets.cmake
        NAMESPACE atp::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AniToolsPlatform
        COMPONENT sdk)

configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/AniToolsPlatformConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/AniToolsPlatformConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AniToolsPlatform)

# ARCH_INDEPENDENT because the package is headers: the pointer-size check the version file would
# otherwise carry compares the machine that installed the SDK against the one consuming it, which
# says nothing about a header. The compatibility that actually matters here — one toolchain and one
# C++ runtime shared by host and plugin — is not expressible in a version file at all, and the
# closest thing to a machine-checkable form of it is the ABI constant below.
write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/AniToolsPlatformConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
        ARCH_INDEPENDENT)

install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/AniToolsPlatformConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/AniToolsPlatformConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AniToolsPlatform
        COMPONENT sdk)

# Installed verbatim rather than configured: the file this tree includes and the file the package
# ships are then literally the same one, which is what keeps atp_add_plugin from drifting away from
# the plugins that exercise it.
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/cmake/AniToolsPlatformPluginHelpers.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AniToolsPlatform
        COMPONENT sdk)
