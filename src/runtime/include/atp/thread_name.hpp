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

// Имя текущего потока для отладчика/профайлера. Вторая точка платформенных
// веток после module_loader. Ошибки глотаются: имя — диагностика, не логика.
#if defined(_WIN32)
inline void set_current_thread_name(const std::string& name) noexcept {
    // SetThreadDescription берётся динамически: заголовки MinGW его не
    // объявляют, а статическая линковка отрезала бы Windows < 10 1607 —
    // для диагностической функции ни то ни другое не оправдано.
    using set_description_fn = HRESULT(WINAPI*)(HANDLE, PCWSTR);
    static const auto set_description = reinterpret_cast<set_description_fn>(
        reinterpret_cast<void*>(::GetProcAddress(::GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription")));
    if (!set_description) {
        return;
    }
    // побайтовое расширение: имена потоков ожидаются ASCII — не-ASCII
    // исказит подпись в отладчике, на логику не влияет
    std::wstring wide(name.begin(), name.end());
    (void)set_description(::GetCurrentThread(), wide.c_str());
}
#else
inline void set_current_thread_name(const std::string& name) noexcept {
    (void)::pthread_setname_np(pthread_self(), name.substr(0, 15).c_str());
}
#endif

}  // namespace atp::detail

#endif  // ANITOOLSPLATFORM_THREAD_NAME_HPP
