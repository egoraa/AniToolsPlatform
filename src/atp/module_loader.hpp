#ifndef ANITOOLSPLATFORM_MODULE_LOADER_HPP
#define ANITOOLSPLATFORM_MODULE_LOADER_HPP

#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <atp/module_registry.hpp>
#include <atp/plugin.hpp>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace atp {

namespace detail {

// Платформенные обёртки над dlopen/LoadLibrary. Хэндл — void*,
// ветвление только здесь; текст системной ошибки уходит в исключение.
#if defined(_WIN32)
inline void* open_library(const std::filesystem::path& path) {
    void* handle = ::LoadLibraryW(path.c_str());
    if (!handle) {
        // system_category на Windows переводит коды GetLastError
        throw std::runtime_error("cannot load plugin '" + path.string() +
                                 "': " + std::system_category().message(static_cast<int>(::GetLastError())));
    }
    return handle;
}

inline void* find_symbol(void* handle, const char* name) noexcept {
    return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(handle), name));
}

inline void close_library(void* handle) noexcept {
    ::FreeLibrary(static_cast<HMODULE>(handle));
}
#else
inline void* open_library(const std::filesystem::path& path) {
    void* handle = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        const char* error = ::dlerror();
        throw std::runtime_error("cannot load plugin '" + path.string() + "': " + (error ? error : "unknown error"));
    }
    return handle;
}

inline void* find_symbol(void* handle, const char* name) noexcept {
    return ::dlsym(handle, name);
}

inline void close_library(void* handle) noexcept {
    ::dlclose(handle);
}
#endif

// RAII-хэндл библиотеки: живёт в shared_ptr, копии которого («пины»)
// держат загрузчик, обёртки фабрик и делетеры созданных модулей.
// Физическая выгрузка — со смертью последнего пина.
class plugin_library {
   public:
    explicit plugin_library(const std::filesystem::path& path) : handle_(open_library(path)) {}
    ~plugin_library() {
        close_library(handle_);
    }

    plugin_library(const plugin_library&) = delete;
    plugin_library& operator=(const plugin_library&) = delete;

    [[nodiscard]] void* find(const char* name) const noexcept {
        return find_symbol(handle_, name);
    }

   private:
    void* handle_;
};

}  // namespace detail

// Загрузка плагина: открыть библиотеку → проверить ABI → зарегистрировать
// модули в переданный реестр. Любой сбой — std::runtime_error, библиотека
// при этом закрывается, частичная регистрация снимается.
//
// Время жизни: реестр живёт дольше загрузчика. Модули, созданные фабриками
// плагина, МОГУТ переживать загрузчик — каждый пинит свою библиотеку через
// делетер module_ptr, физическая выгрузка происходит со смертью последнего
// модуля.
class module_loader {
   public:
    module_loader(const std::filesystem::path& library, module_registry& registry)
        : path_(library), registry_(&registry) {
        library_ = std::make_shared<detail::plugin_library>(path_);
        module_registrar registrar{registry, library_};
        try {
            const auto abi = load_symbol<abi_version_fn>(abi_version_symbol);
            if (const unsigned plugin_version = abi(); plugin_version != plugin_abi) {
                throw std::runtime_error("plugin '" + path_.string() + "' has ABI " + std::to_string(plugin_version) +
                                         ", host expects " + std::to_string(plugin_abi));
            }
            const auto register_modules = load_symbol<register_modules_fn>(register_modules_symbol);
            register_modules(registrar);
            modules_ = registrar.registered();
        } catch (const std::exception& e) {
            // Исключение могло родиться в коде плагина (инлайны registrar
            // инстанцируются в DLL): его нельзя ни перебрасывать, ни
            // разрушать после выгрузки библиотеки. Текст копируется в
            // хост-исключение, пока DLL жива; плагинное исключение умирает
            // на выходе из catch, и только затем раскрутка конструктора
            // (член library_) выгружает библиотеку. Частичная регистрация
            // снимается: только свои пары (имя, версия) — чужие версии тех
            // же имён не задеваются.
            for (const auto& [name, ver] : registrar.registered()) {
                registry.remove(name, ver);
            }
            throw std::runtime_error(e.what());
        } catch (...) {
            for (const auto& [name, ver] : registrar.registered()) {
                registry.remove(name, ver);
            }
            throw std::runtime_error("plugin '" + path_.string() + "' registration failed");
        }
    }

    ~module_loader() {
        unload();
    }

    module_loader(const module_loader&) = delete;
    module_loader& operator=(const module_loader&) = delete;

    module_loader(module_loader&& other) noexcept
        : path_(std::move(other.path_))
        , registry_(other.registry_)
        , library_(std::move(other.library_))
        , modules_(std::move(other.modules_)) {}

    module_loader& operator=(module_loader&& other) noexcept {
        if (this != &other) {
            unload();
            path_ = std::move(other.path_);
            registry_ = other.registry_;
            library_ = std::move(other.library_);
            modules_ = std::move(other.modules_);
        }
        return *this;
    }

    // Пары (имя, версия) модулей, зарегистрированных этим плагином.
    [[nodiscard]] const std::vector<std::pair<std::string, version>>& modules() const {
        return modules_;
    }
    [[nodiscard]] const std::filesystem::path& path() const {
        return path_;
    }

   private:
    template <typename TFn>
    TFn* load_symbol(const char* name) const {
        void* symbol = library_->find(name);
        if (!symbol) {
            throw std::runtime_error("plugin '" + path_.string() + "' has no symbol '" + name + "'");
        }
        return reinterpret_cast<TFn*>(symbol);
    }

    void unload() noexcept {
        if (!library_) {
            return;
        }
        // сначала фабрики — их vtable лежат в ещё загруженной библиотеке;
        // снимаются точечно по (имя, версия)
        for (const auto& [name, ver] : modules_) {
            registry_->remove(name, ver);
        }
        modules_.clear();
        // отпускаем свой пин: физическая выгрузка — когда умрёт последний
        // модуль этой библиотеки (или прямо сейчас, если их нет)
        library_.reset();
    }

    std::filesystem::path path_;
    module_registry* registry_;
    std::shared_ptr<detail::plugin_library> library_;
    std::vector<std::pair<std::string, version>> modules_;
};

}  // namespace atp

#endif  // ANITOOLSPLATFORM_MODULE_LOADER_HPP
