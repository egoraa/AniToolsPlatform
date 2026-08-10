// SPDX-License-Identifier: Apache-2.0
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
/// properties as the third ports section, exposed by module_base::properties(); 9 — module_context
/// carries a module_host next to the services, which is where a module logs and wakes its thread, and
/// an output no longer caches the value it wrote, so peek() and the replay connect are gone;
/// 10 — every input counts what it received and lost (input_base::stats()) and a queueing one takes
/// a capacity with an overflow policy instead of growing without a limit; 11 — create() takes a
/// config_value.
///
/// 11 is not a return of 6, which is worth spelling out or the history reads as a circle. What 6
/// carried was per-instance *scalars* as a string, and 8 replaced them with properties for good
/// reason: a property shows up in the inspector, is edited live and has a codec to text and back.
/// None of that helps a setting which is not a scalar, is needed before initialize and is never
/// edited live — the niche 8 left open and 11 fills, with a structured value instead of a string.
///
/// Bumping this number is also a change to templates/plugin/CMakeLists.txt, which names the ABI it
/// was written for and is meant to stop configuring until the plugin there has been looked at. CMake
/// reads the value below out of this file (cmake/Install.cmake) to export it as a package constant,
/// so the line must stay in this exact shape.
inline constexpr unsigned plugin_abi = 11;

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

/// Identity of the toolchain and standard library a binary was built with — the one incompatibility
/// the ABI number cannot see.
///
/// Host and plugin share allocations and standard library statics, so a mismatch here corrupts memory
/// rather than failing to load: on MSVC a Debug host and a Release plugin differ in
/// _ITERATOR_DEBUG_LEVEL, which changes the layout of the containers both sides touch, and nothing
/// diagnoses it. `atp_add_plugin()` warns about the static CRT at configure time, but a plugin built
/// by another project — the case this whole path exists for — never runs that check. Comparing this
/// string at load turns the worst failure mode the platform has into a message.
#define ATP_STRINGIFY_IMPL(x) #x
#define ATP_STRINGIFY(x) ATP_STRINGIFY_IMPL(x)

#if defined(_MSC_VER)
#define ATP_BUILD_ID_COMPILER "msvc-" ATP_STRINGIFY(_MSC_VER)
#elif defined(__clang__)
#define ATP_BUILD_ID_COMPILER "clang-" ATP_STRINGIFY(__clang_major__)
#elif defined(__GNUC__)
#define ATP_BUILD_ID_COMPILER "gcc-" ATP_STRINGIFY(__GNUC__)
#else
#define ATP_BUILD_ID_COMPILER "unknown-compiler"
#endif

#if defined(_ITERATOR_DEBUG_LEVEL)
#define ATP_BUILD_ID_STDLIB "-msvcstl-idl" ATP_STRINGIFY(_ITERATOR_DEBUG_LEVEL)
#elif defined(_LIBCPP_VERSION)
#define ATP_BUILD_ID_STDLIB "-libcxx-" ATP_STRINGIFY(_LIBCPP_VERSION)
#elif defined(_GLIBCXX_USE_CXX11_ABI)
#define ATP_BUILD_ID_STDLIB "-libstdcxx-abi" ATP_STRINGIFY(_GLIBCXX_USE_CXX11_ABI)
#else
#define ATP_BUILD_ID_STDLIB "-unknown-stdlib"
#endif

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define ATP_BUILD_ID_ASAN "-asan"
#endif
#if __has_feature(thread_sanitizer)
#define ATP_BUILD_ID_TSAN "-tsan"
#endif
#endif
#if defined(__SANITIZE_ADDRESS__) && !defined(ATP_BUILD_ID_ASAN)
#define ATP_BUILD_ID_ASAN "-asan"
#endif
#if defined(__SANITIZE_THREAD__) && !defined(ATP_BUILD_ID_TSAN)
#define ATP_BUILD_ID_TSAN "-tsan"
#endif
#ifndef ATP_BUILD_ID_ASAN
#define ATP_BUILD_ID_ASAN ""
#endif
#ifndef ATP_BUILD_ID_TSAN
#define ATP_BUILD_ID_TSAN ""
#endif

namespace atp {

/// Build identity of this binary, assembled from the macros above.
inline constexpr const char* plugin_build_id =
    ATP_BUILD_ID_COMPILER ATP_BUILD_ID_STDLIB ATP_BUILD_ID_ASAN ATP_BUILD_ID_TSAN;

}  // namespace atp

/// Cross-platform export of a C symbol from a plugin.
#if defined(_WIN32)
#define ATP_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define ATP_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

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

#endif
