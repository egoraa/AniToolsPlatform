// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_PLUGIN_HANDSHAKE_HPP
#define ANITOOLSPLATFORM_PLUGIN_HANDSHAKE_HPP

#include <atp/plugin/abi.hpp>
#include <atp/plugin/build_id.hpp>

/// Cross-platform export of a C symbol from a plugin.
#if defined(_WIN32)
#define ATP_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define ATP_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// NOLINTBEGIN(cppcoreguidelines-macro-usage)
/// Defines both handshake symbols, which have no plugin-specific content and are the same two
/// functions in every plugin ever written. Writing them out by hand stays legal — this only takes the
/// boilerplate away, and it is how a plugin picks up a future handshake symbol without an edit.
///
///     ATP_PLUGIN_HANDSHAKE()
///
///     ATP_PLUGIN_EXPORT void atp_register_modules(atp::module_registrar& registrar) {
///         registrar.add<my_module>();
///     }
#define ATP_PLUGIN_HANDSHAKE()                     \
    ATP_PLUGIN_EXPORT unsigned atp_abi_version() { \
        return atp::plugin_abi;                    \
    }                                              \
    ATP_PLUGIN_EXPORT const char* atp_build_id() { \
        return atp::plugin_build_id;               \
    }
// NOLINTEND(cppcoreguidelines-macro-usage)

#endif
