// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_PLUGIN_ABI_HPP
#define ANITOOLSPLATFORM_PLUGIN_ABI_HPP

#include <atp/hosting/module_registrar.hpp>

namespace atp {

/// Platform ABI: bumped on any incompatible change to the contracts a plugin sees (module_base,
/// the factories, the io types). It does not protect against a foreign compiler or runtime — plugins
/// must be built with the host's toolchain (on MSVC, sharing the CRT via /MD).
///
/// The number starts at 1 because nothing has been published yet: an ABI number refuses a binary
/// built against an older contract, and there is no such binary outside this repository. Once the
/// SDK ships, every incompatible change owes a bump, and the first one is answered by 2.
///
/// Splitting io::property_kind::number into integer and real is the worked example of what that
/// will mean. A plugin compiles the value of that enumeration into itself — property_base is built
/// inside the plugin from property_codec<T>::kind and the host reads the value back — so a plugin
/// built against the older header stores 0 for a real property and a newer host reads 0 as integer.
/// It cost no bump here for the reason above and would cost 2 on a published SDK.
///
/// Reusing 1 has one sharp edge worth naming. A plugin built against the *historical* ABI 1 — a
/// contract sharing nothing with this one — would pass the equality check the loader makes and then
/// be called through a different module_base vtable, which is corruption rather than a refused load.
/// atp_build_id would catch it, but that symbol postdates that ABI and its absence is tolerated
/// silently. Nothing outside an archived build tree can hold such a binary, so this is a note and
/// not a guard.
///
/// Bumping this number is also a change to templates/plugin/CMakeLists.txt, which names the ABI it
/// was written for and is meant to stop configuring until the plugin there has been looked at. CMake
/// reads the value below out of this file (cmake/Install.cmake) to export it as a package constant,
/// so the line must stay in this exact shape.
inline constexpr unsigned plugin_abi = 1;

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
