// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_PLUGIN_BUILD_ID_HPP
#define ANITOOLSPLATFORM_PLUGIN_BUILD_ID_HPP

/// Identity of the toolchain and standard library a binary was built with — the one incompatibility
/// the ABI number cannot see.
///
/// Host and plugin share allocations and standard library statics, so a mismatch here corrupts memory
/// rather than failing to load: on MSVC a Debug host and a Release plugin differ in
/// _ITERATOR_DEBUG_LEVEL, which changes the layout of the containers both sides touch, and nothing
/// diagnoses it. `atp_add_plugin()` warns about the static CRT at configure time, but a plugin built
/// by another project — the case this whole path exists for — never runs that check. Comparing this
/// string at load turns the worst failure mode the platform has into a message.
#define ATP_STRINGIFY_IMPL(x) #x                // NOLINT(cppcoreguidelines-macro-usage)
#define ATP_STRINGIFY(x) ATP_STRINGIFY_IMPL(x)  // NOLINT(cppcoreguidelines-macro-usage)

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
#define ATP_BUILD_ID_STDLIB \
    "-msvcstl-idl" ATP_STRINGIFY(_ITERATOR_DEBUG_LEVEL)  // NOLINT(cppcoreguidelines-macro-usage)
#elif defined(_LIBCPP_VERSION)
#define ATP_BUILD_ID_STDLIB "-libcxx-" ATP_STRINGIFY(_LIBCPP_VERSION)  // NOLINT(cppcoreguidelines-macro-usage)
#elif defined(_GLIBCXX_USE_CXX11_ABI)
#define ATP_BUILD_ID_STDLIB \
    "-libstdcxx-abi" ATP_STRINGIFY(_GLIBCXX_USE_CXX11_ABI)  // NOLINT(cppcoreguidelines-macro-usage)
#else
#define ATP_BUILD_ID_STDLIB "-unknown-stdlib"  // NOLINT(cppcoreguidelines-macro-usage)
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
#define ATP_BUILD_ID_ASAN ""  // NOLINT(cppcoreguidelines-macro-usage)
#endif
#ifndef ATP_BUILD_ID_TSAN
#define ATP_BUILD_ID_TSAN ""  // NOLINT(cppcoreguidelines-macro-usage)
#endif

namespace atp {

/// Build identity of this binary, assembled from the macros above.
inline constexpr const char* plugin_build_id =
    ATP_BUILD_ID_COMPILER ATP_BUILD_ID_STDLIB ATP_BUILD_ID_ASAN ATP_BUILD_ID_TSAN;

}  // namespace atp

#endif
