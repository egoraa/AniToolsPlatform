// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_PLUGIN_ABI_HPP
#define ANITOOLSPLATFORM_PLUGIN_ABI_HPP

#include <atp/hosting/module_registrar.hpp>

namespace atp {

/// Platform ABI: bumped on any incompatible change to the contracts a plugin sees (module_base,
/// the factories, the io types). It does not protect against a foreign compiler or runtime — plugins
/// must be built with the host's toolchain (on MSVC, sharing the CRT via /MD).
///
/// What each number changed, and why 11 was not a return of 6, is the section "История plugin_abi" in
/// docs/architecture.md — a version list is rationale, and rationale lives in the document rather
/// than in a comment that grows by a paragraph per bump.
///
/// Bumping this number is also a change to templates/plugin/CMakeLists.txt, which names the ABI it
/// was written for and is meant to stop configuring until the plugin there has been looked at. CMake
/// reads the value below out of this file (cmake/Install.cmake) to export it as a package constant,
/// so the line must stay in this exact shape.
inline constexpr unsigned plugin_abi = 14;

/// The plugin contract is two C symbols:
///
///     ATP_PLUGIN_EXPORT unsigned atp_abi_version();
///     ATP_PLUGIN_EXPORT void atp_register_modules(atp::module_registrar&);
///
/// The atp_abi_version handshake is the only call that is safe under any mismatch: plain C, no
/// parameters and no C++ types. Registration is already a C++ call and is allowed only once the ABI
/// versions match.
///
/// A third symbol, atp_build_id, is optional and describes the toolchain (see plugin_build_id). A
/// plugin that omits it loads as before, so it is not part of the ABI number.
///
/// A plugin that is not C++ at all takes the other path entirely — three pure C symbols and no
/// header of this SDK beyond atp/plugin_c.h.
using abi_version_fn = unsigned();
using register_modules_fn = void(module_registrar&);
using build_id_fn = const char*();

/// Symbol names as constants, so that the loader and the tests do not duplicate the strings.
inline constexpr const char* abi_version_symbol = "atp_abi_version";
inline constexpr const char* register_modules_symbol = "atp_register_modules";
inline constexpr const char* build_id_symbol = "atp_build_id";

}  // namespace atp

#endif
