#ifndef ANITOOLSPLATFORM_THREAD_NAME_HPP
#define ANITOOLSPLATFORM_THREAD_NAME_HPP

#include <string>

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

namespace atp::detail {

/// Names the current thread for the debugger and the profiler. Errors are swallowed: a thread name
/// is diagnostics, not logic.
#if defined(_WIN32)
inline void set_current_thread_name(const std::string& name) noexcept {
    using set_description_fn = HRESULT(WINAPI*)(HANDLE, PCWSTR);
    static const auto set_description = reinterpret_cast<set_description_fn>(
        reinterpret_cast<void*>(::GetProcAddress(::GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription")));
    if (!set_description) {
        return;
    }
    std::wstring wide(name.begin(), name.end());
    (void)set_description(::GetCurrentThread(), wide.c_str());
}
#elif defined(__APPLE__)
/// Names the current thread for the debugger and the profiler; the limit is 63 characters.
inline void set_current_thread_name(const std::string& name) noexcept {
    (void)::pthread_setname_np(name.substr(0, 63).c_str());
}
#else
/// Names the current thread for the debugger and the profiler; the limit is 15 characters.
inline void set_current_thread_name(const std::string& name) noexcept {
    (void)::pthread_setname_np(pthread_self(), name.substr(0, 15).c_str());
}
#endif

}  // namespace atp::detail

#endif
