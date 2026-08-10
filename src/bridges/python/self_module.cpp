// SPDX-License-Identifier: Apache-2.0
#include "self_module.hpp"

#include <string>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace atp::bridge {

std::string to_utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}
namespace {

#if defined(_WIN32)
HMODULE self_handle(DWORD extra_flags) {
    HMODULE handle = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | extra_flags, reinterpret_cast<LPCWSTR>(&self_directory),
                       &handle);
    return handle;
}
#endif

}  // namespace

std::filesystem::path self_directory() {
#if defined(_WIN32)
    const HMODULE handle = self_handle(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT);
    if (handle == nullptr) {
        return {};
    }
    wchar_t buffer[MAX_PATH]{};
    const DWORD written = GetModuleFileNameW(handle, buffer, MAX_PATH);
    if (written == 0 || written == MAX_PATH) {
        return {};
    }
    return std::filesystem::path(buffer).parent_path();
#else
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&self_directory), &info) == 0 || info.dli_fname == nullptr) {
        return {};
    }
    return std::filesystem::path(info.dli_fname).parent_path();
#endif
}

void pin_self() {
#if defined(_WIN32)
    self_handle(GET_MODULE_HANDLE_EX_FLAG_PIN);
#else
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&self_directory), &info) != 0 && info.dli_fname != nullptr) {
        dlopen(info.dli_fname, RTLD_LAZY | RTLD_NODELETE);
    }
#endif
}

}  // namespace atp::bridge
