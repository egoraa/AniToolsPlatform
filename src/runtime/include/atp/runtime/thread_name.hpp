// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_RUNTIME_THREAD_NAME_HPP
#define ATP_RUNTIME_THREAD_NAME_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace atp::runtime::detail {

/// Longest thread name any of the platforms here accepts, plus room for the terminator. The buffers
/// below are fixed for a reason: this whole file is noexcept and runs as the first statement of a
/// worker thread, where a std::bad_alloc would escape a noexcept function and take the process down.
/// A name is diagnostics, so a long one is truncated rather than reported.
inline constexpr std::size_t max_thread_name = 63;

/// Copies at most `limit` characters into a fixed buffer and terminates it, widening each character
/// on the way when the target is wide. Allocation-free by construction.
template <typename TChar>
inline void copy_thread_name(std::string_view name, TChar* buffer, std::size_t limit) noexcept {
    const std::size_t count = std::min(name.size(), limit);
    for (std::size_t i = 0; i < count; ++i) {
        buffer[i] = static_cast<TChar>(name[i]);
    }
    buffer[count] = TChar{};
}

/// Names the current thread for the debugger and the profiler; a name longer than 63 characters is
/// truncated. Errors are swallowed: a thread name is diagnostics, not logic.
#if defined(_WIN32)
inline void set_current_thread_name(std::string_view name) noexcept {
    using set_description_fn = HRESULT(WINAPI*)(HANDLE, PCWSTR);
    static const auto set_description = reinterpret_cast<set_description_fn>(
        // NOLINTNEXTLINE(bugprone-casting-through-void)
        reinterpret_cast<void*>(::GetProcAddress(::GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription")));
    if (!set_description) {
        return;
    }
    std::array<wchar_t, max_thread_name + 1> wide{};
    copy_thread_name(name, wide.data(), max_thread_name);
    (void)set_description(::GetCurrentThread(), wide.data());
}
#elif defined(__APPLE__)
/// Names the current thread for the debugger and the profiler; the limit is 63 characters.
inline void set_current_thread_name(std::string_view name) noexcept {
    std::array<char, max_thread_name + 1> buffer{};
    copy_thread_name(name, buffer.data(), max_thread_name);
    (void)::pthread_setname_np(buffer.data());
}
#else
/// Names the current thread for the debugger and the profiler; the limit is 15 characters.
inline void set_current_thread_name(std::string_view name) noexcept {
    std::array<char, 16> buffer{};
    copy_thread_name(name, buffer.data(), 15);
    (void)::pthread_setname_np(pthread_self(), buffer.data());
}
#endif

}  // namespace atp::runtime::detail

#endif
