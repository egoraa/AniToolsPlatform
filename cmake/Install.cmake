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

# The ABI number stays in C++ — plugin/abi.hpp is where it is declared, bumped and read by the
# handshake — and CMake reads it from there, so the package constant cannot drift from the header.
# Exactly one line may match: a second one would mean the header no longer has a single answer, and
# guessing which is right is worse than stopping.
set(_atp_plugin_header ${CMAKE_CURRENT_SOURCE_DIR}/include/atp/plugin/abi.hpp)
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

# The license, the NOTICE, the list of holders it names and the third-party notices, in **both**
# components — the only rule in this file that is not about the SDK alone, and it is here because this
# is the one place that runs whenever ATP_INSTALL is on, whichever subdirectories contributed install
# rules. AUTHORS travels with them because NOTICE points at it: a reference to a file the package does
# not contain is worse than no reference.
#
# Both, because each component is a distribution in its own right: the applications carry binaries with
# nlohmann/json inside them and, for the studio, the Qt runtime beside them, while the SDK hands a
# plugin author a copy of the headers — and section 4(a) of the Apache License asks whoever receives
# them to receive the license too. Installing everything writes the three files twice with identical
# content, which is why the duplication is not worth avoiding.
#
# The destination is the prefix root rather than CMAKE_INSTALL_DOCDIR: the delivery is a portable
# archive a person unpacks, not a distribution package, and share/doc/AniToolsPlatform is not where
# either a reader or a lawyer looks first.
set(_atp_license_files
        ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE
        ${CMAKE_CURRENT_SOURCE_DIR}/NOTICE
        ${CMAKE_CURRENT_SOURCE_DIR}/AUTHORS
        ${CMAKE_CURRENT_SOURCE_DIR}/THIRD-PARTY-NOTICES.md)

install(FILES ${_atp_license_files} DESTINATION . COMPONENT sdk)
install(FILES ${_atp_license_files} DESTINATION . COMPONENT applications)

# The templates are part of the SDK rather than a curiosity of the repository: templates/plugin
# reaches the platform through find_package and cannot be configured at all without an installed
# prefix to point at, and the Rust one is written against a header this package ships. They ride in
# the `sdk` component for that reason — whoever asks for the headers is exactly whoever needs them.
#
# The exclusions are not hygiene but correctness: templates/plugin_rust/target and Cargo.lock exist in
# a developer's tree and are listed in .gitignore, which install(DIRECTORY) knows nothing about, so
# without them a release built on a machine that once ran cargo would carry that machine's build
# output. The regex covers the same accident on the C++ side.
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/templates/
        DESTINATION templates
        COMPONENT sdk
        PATTERN "target" EXCLUDE
        PATTERN "Cargo.lock" EXCLUDE
        REGEX "/(build|cmake-build-[^/]*)$" EXCLUDE)

# The package is copied into the Python template as well as next to the bridge, and the two copies
# answer different questions. The one beside the bridge is what `import atp` resolves to at run time;
# this one is what an author's editor resolves it to while the module is being written, before
# anything has been built or installed a second time. It is not a fork — nothing edits it, and both
# rules name the same directory — so there is no drift for a test to catch, which is exactly how it
# differs from the hand-written mirror in templates/plugin_rust/src/abi.rs.
#
# Unconditional on purpose: src/bridges/python is added only under ATP_BUILD_PYTHON_BRIDGE and returns
# early without CPython development files, and neither of those has anything to do with a person
# reading the package in an editor.
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/bridges/python/package/atp
        DESTINATION templates/plugin_python
        COMPONENT sdk)
