// SPDX-License-Identifier: Apache-2.0
#include "self_module.hpp"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace atp::lua_bridge {

std::filesystem::path self_directory() {
#if defined(_WIN32)
    HMODULE handle = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&self_directory), &handle);
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

}  // namespace atp::lua_bridge
