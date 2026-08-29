// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_RUNTIME_MODULE_LOADER_HPP
#define ATP_RUNTIME_MODULE_LOADER_HPP

#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <atp/hosting/module_registrar.hpp>
#include <atp/hosting/module_registry.hpp>
#include <atp/plugin.hpp>
#include <atp/runtime/c_module.hpp>
#include <atp/runtime/utf8_path.hpp>

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

namespace atp::runtime {

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
        throw std::runtime_error("cannot load plugin '" + path_to_utf8(path) +
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
/// Opens a plugin library and reports the platform's own error text on failure.
///
/// POSIX does not guarantee dlerror() to be thread safe, and that is accepted here rather than
/// worked around: loading a plugin fills module_registry, which is setup-phase-only and not thread
/// safe either, so a second thread cannot be in this code to begin with.
inline void* open_library(const std::filesystem::path& path) {
    void* handle = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        const char* error = ::dlerror();
        throw std::runtime_error("cannot load plugin '" + path_to_utf8(path) +
                                 "': " + (error ? error : "unknown error"));
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

/// A file that opened as a library but exports neither entry point: not a plugin at all, as opposed
/// to a plugin that is broken. The distinction exists for scanning a directory — a foreign library
/// lying next to the plugins is skipped, while a plugin whose ABI does not match must stop the host.
/// Derived from std::runtime_error, so every handler written before this type keeps catching it.
class not_a_plugin : public std::runtime_error {
   public:
    using std::runtime_error::runtime_error;
};

/// One module a plugin registered, with the file it declares itself in.
///
/// `source` is empty for everything compiled into a plugin, and carries a path only where a plugin
/// builds its modules out of files — the Python bridge and its scripts. It rides here rather than in
/// the factory because a factory is a plugin ABI type: growing it would be an ABI break, while this
/// list is the host's own.
struct registered_module {
    std::string name;
    version ver;
    std::string source;

    friend bool operator==(const registered_module&, const registered_module&) = default;
};

/// Plugin loader: opens the library, checks the ABI and registers the plugin's modules into the
/// given registry. The registry has to outlive the loader, while the modules created by the
/// plugin's factories MAY outlive it — each pins its library through the module_ptr deleter, and
/// the physical unload happens when the last of them dies.
///
/// Two registration paths are recognised, and which one a plugin took is decided by the symbols it
/// exports rather than by anything in the config: the C++ pair of plugin.hpp, and the pure C triple of
/// plugin_c.h for a plugin that is not C++. A plugin exporting both is legal and both are run — a
/// bridge may well offer modules of its own next to the foreign ones it wraps.
class module_loader {
   public:
    /// Loads a plugin and registers its modules.
    /// @param library path to the plugin; the platform extension may be omitted
    /// @param registry registry the factories go into; it must outlive this loader
    /// @throws not_a_plugin if the file exports neither entry point
    /// @throws std::runtime_error on any other failure — the library is closed again and a partial
    ///         registration is withdrawn
    ///
    /// Every failure but not_a_plugin comes out as a plain std::runtime_error naming this file, and
    /// flattening the type that way is deliberate: the caller tells apart exactly two cases — a
    /// foreign library, which a directory scan skips, and a broken plugin, which stops the host. The
    /// file name is added here and only here, so no message carries it twice.
    module_loader(const std::filesystem::path& library, module_registry& registry)
        : path_(detail::with_plugin_extension(library)), registry_(&registry) {
        library_ = std::make_shared<detail::plugin_library>(path_);
        module_registrar registrar{registry, library_};
        try {
            const bool speaks_c = library_->find(c_abi_version_symbol) != nullptr;
            const bool speaks_cxx = library_->find(abi_version_symbol) != nullptr;
            if (!speaks_c && !speaks_cxx) {
                throw not_a_plugin("plugin '" + shown() + "' exports neither '" + abi_version_symbol + "' nor '" +
                                   c_abi_version_symbol + "'");
            }
            std::vector<c_registration> from_c;
            if (speaks_c) {
                from_c = register_c_path(registrar);
            }
            if (speaks_cxx) {
                register_cxx_path(registrar);
            }
            modules_.reserve(registrar.registered().size());
            for (const auto& [name, ver] : registrar.registered()) {
                modules_.push_back({name, ver, source_of(from_c, name, ver)});
            }
        } catch (const not_a_plugin&) {
            for (const auto& [name, ver] : registrar.registered()) {
                registry.remove(name, ver);
            }
            throw;
        } catch (const std::exception& e) {
            for (const auto& [name, ver] : registrar.registered()) {
                registry.remove(name, ver);
            }
            throw std::runtime_error("plugin '" + shown() + "': " + e.what());
        } catch (...) {
            for (const auto& [name, ver] : registrar.registered()) {
                registry.remove(name, ver);
            }
            throw std::runtime_error("plugin '" + shown() + "' registration failed");
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

    /// The modules this plugin registered, in registration order.
    [[nodiscard]] const std::vector<registered_module>& modules() const {
        return modules_;
    }

    /// Path the plugin was loaded from, extension included.
    [[nodiscard]] const std::filesystem::path& path() const {
        return path_;
    }

   private:
    /// This loader's file as UTF-8, for the messages. Never path::string(): that conversion goes
    /// through the process code page, which spells a non-ASCII name in bytes nothing downstream
    /// expects and throws outright for a character the page cannot represent — inside the very
    /// message that was reporting the real failure.
    [[nodiscard]] std::string shown() const {
        return path_to_utf8(path_);
    }

    /// Registers the modules of a plugin that is not C++.
    [[nodiscard]] std::vector<c_registration> register_c_path(module_registrar& registrar) const {
        return register_c_modules(registrar, load_symbol<c_abi_version_fn>(c_abi_version_symbol),
                                  load_symbol<c_module_count_fn>(c_module_count_symbol),
                                  load_symbol<c_module_desc_at_fn>(c_module_desc_at_symbol));
    }

    [[nodiscard]] static std::string source_of(const std::vector<c_registration>& from_c,
                                               const std::string& name,
                                               version ver) {
        for (const c_registration& made : from_c) {
            if (made.name == name && made.ver == ver) {
                return made.source;
            }
        }
        return {};
    }

    void register_cxx_path(module_registrar& registrar) const {
        const auto abi = load_symbol<abi_version_fn>(abi_version_symbol);
        if (const unsigned plugin_version = abi(); plugin_version != plugin_abi) {
            throw std::runtime_error("ABI is " + std::to_string(plugin_version) + ", host expects " +
                                     std::to_string(plugin_abi));
        }
        check_build_id();
        load_symbol<register_modules_fn>(register_modules_symbol)(registrar);
    }

    /// Refuses a plugin built by another toolchain or against another standard library configuration.
    ///
    /// Silent when the symbol is absent, which is what keeps every plugin built before it existed
    /// loading unchanged — the loader has no host to warn through, and turning a missing optional
    /// symbol into a failure would be an ABI break dressed up as a diagnostic. What it does catch is
    /// the case the ABI number cannot: same version, incompatible memory layout.
    void check_build_id() const {
        void* symbol = library_->find(build_id_symbol);
        if (symbol == nullptr) {
            return;
        }
        const char* id = reinterpret_cast<build_id_fn*>(symbol)();
        if (id != nullptr && std::strcmp(id, plugin_build_id) == 0) {
            return;
        }
        throw std::runtime_error("was built as '" + std::string(id == nullptr ? "<none>" : id) + "', host as '" +
                                 plugin_build_id + "'; host and plugin must share one toolchain and one C++ runtime");
    }

    template <typename TFn>
    TFn* load_symbol(const char* name) const {
        void* symbol = library_->find(name);
        if (!symbol) {
            throw std::runtime_error("no symbol '" + std::string(name) + "'");
        }
        return reinterpret_cast<TFn*>(symbol);
    }

    void unload() noexcept {
        if (!library_) {
            return;
        }
        for (const registered_module& m : modules_) {
            registry_->remove(m.name, m.ver);
        }
        modules_.clear();
        library_.reset();
    }

    std::filesystem::path path_;
    module_registry* registry_;
    std::shared_ptr<detail::plugin_library> library_;
    std::vector<registered_module> modules_;
};

}  // namespace atp::runtime

#endif
