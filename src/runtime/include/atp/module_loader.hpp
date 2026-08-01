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

/// Platform extension of a plugin file. A config may write a plugin path without one, so this is
/// what module_loader appends and what a plugin directory is scanned for.
inline constexpr const char* plugin_extension =
#if defined(_WIN32)
    ".dll";
#elif defined(__APPLE__)
    ".dylib";
#else
    ".so";
#endif

namespace detail {

[[nodiscard]] inline std::filesystem::path with_plugin_extension(std::filesystem::path path) {
    if (!path.has_extension()) {
        path += plugin_extension;
    }
    return path;
}

#if defined(_WIN32)
inline void* open_library(const std::filesystem::path& path) {
    void* handle = ::LoadLibraryW(path.c_str());
    if (!handle) {
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

/// Plugin loader: opens the library, checks the ABI and registers the plugin's modules into the
/// given registry. The registry has to outlive the loader, while the modules created by the
/// plugin's factories MAY outlive it — each pins its library through the module_ptr deleter, and
/// the physical unload happens when the last of them dies.
class module_loader {
   public:
    /// Loads a plugin and registers its modules.
    /// @param library path to the plugin; the platform extension may be omitted
    /// @param registry registry the factories go into; it must outlive this loader
    /// @throws std::runtime_error on any failure — the library is closed again and a partial
    ///         registration is withdrawn
    module_loader(const std::filesystem::path& library, module_registry& registry)
        : path_(detail::with_plugin_extension(library)), registry_(&registry) {
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

    /// (name, version) pairs of the modules this plugin registered.
    [[nodiscard]] const std::vector<std::pair<std::string, version>>& modules() const {
        return modules_;
    }

    /// Path the plugin was loaded from, extension included.
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
        for (const auto& [name, ver] : modules_) {
            registry_->remove(name, ver);
        }
        modules_.clear();
        library_.reset();
    }

    std::filesystem::path path_;
    module_registry* registry_;
    std::shared_ptr<detail::plugin_library> library_;
    std::vector<std::pair<std::string, version>> modules_;
};

}  // namespace atp

#endif
