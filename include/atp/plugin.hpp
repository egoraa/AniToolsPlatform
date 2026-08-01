#ifndef ANITOOLSPLATFORM_PLUGIN_HPP
#define ANITOOLSPLATFORM_PLUGIN_HPP

#include <atp/module_registry.hpp>

namespace atp {

/// Platform ABI: bumped on any incompatible change to the contracts a plugin sees (module_base,
/// the factories, the io types). It does not protect against a foreign compiler or runtime — plugins
/// must be built with the host's toolchain (on MSVC, sharing the CRT via /MD).
///
/// History: 2 — the lifecycle takes module_context&; 3 — pull-model inputs, create() returns
/// module_ptr, module_base exposes the io registries, iterate returns work_status; 4 — start()/stop()
/// lose their parameters, the context is given in initialize only; 5 — NVI delivery (do_deliver)
/// plus notifier_base/set_notifier on input_base; 6 — create(config) carrying per-instance
/// parameters as a config string; 7 — output observation via peek()/write_count(); 8 — module
/// properties as the third ports section, exposed by module_base::properties().
inline constexpr unsigned plugin_abi = 8;

/// The plugin contract is two C symbols:
///
///     ATP_PLUGIN_EXPORT unsigned atp_abi_version();
///     ATP_PLUGIN_EXPORT void atp_register_modules(atp::module_registrar&);
///
/// The atp_abi_version handshake is the only call that is safe under any mismatch: plain C, no
/// parameters and no C++ types. Registration is already a C++ call and is allowed only once the ABI
/// versions match.
using abi_version_fn = unsigned();
using register_modules_fn = void(module_registrar&);

/// Symbol names as constants, so that the loader and the tests do not duplicate the strings.
inline constexpr const char* abi_version_symbol = "atp_abi_version";
inline constexpr const char* register_modules_symbol = "atp_register_modules";

}  // namespace atp

/// Cross-platform export of a C symbol from a plugin.
#if defined(_WIN32)
#define ATP_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define ATP_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

#endif
