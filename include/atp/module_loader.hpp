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
                throw std::runtime_error(
                    "cannot load plugin '" + path.string() + "': " +
                    std::system_category().message(static_cast<int>(::GetLastError())));
            }
            return handle;
        }

        inline void* find_symbol(void* handle, const char* name) noexcept {
            return reinterpret_cast<void*>(
                ::GetProcAddress(static_cast<HMODULE>(handle), name));
        }

        inline void close_library(void* handle) noexcept {
            ::FreeLibrary(static_cast<HMODULE>(handle));
        }
#else
        inline void* open_library(const std::filesystem::path& path) {
            void* handle = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
            if (!handle) {
                const char* error = ::dlerror();
                throw std::runtime_error("cannot load plugin '" + path.string() +
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

    } // namespace detail

    // Загрузка плагина: открыть библиотеку → проверить ABI → зарегистрировать
    // модули в переданный реестр. Любой сбой — std::runtime_error, библиотека
    // при этом закрывается, частичная регистрация снимается.
    //
    // Время жизни (контракт на вызывающем, как у соединений io): реестр живёт
    // дольше загрузчика; модули, созданные фабриками плагина, разрушаются до
    // разрушения загрузчика — их код лежит в выгружаемой библиотеке.
    class module_loader {
    public:
        module_loader(const std::filesystem::path& library, module_registry& registry)
            : path_(library), registry_(&registry) {
            handle_ = detail::open_library(path_);
            module_registrar registrar{registry};
            try {
                const auto abi = load_symbol<abi_version_fn>(abi_version_symbol);
                if (const unsigned plugin_version = abi(); plugin_version != plugin_abi) {
                    throw std::runtime_error(
                        "plugin '" + path_.string() + "' has ABI " +
                        std::to_string(plugin_version) + ", host expects " +
                        std::to_string(plugin_abi));
                }
                const auto register_modules =
                    load_symbol<register_modules_fn>(register_modules_symbol);
                register_modules(registrar);
                modules_ = registrar.registered();
            } catch (...) {
                // частичная регистрация не должна пережить закрытие библиотеки
                for (const auto& name : registrar.registered()) {
                    registry.remove(name);
                }
                detail::close_library(handle_);
                handle_ = nullptr;
                throw;
            }
        }

        ~module_loader() { unload(); }

        module_loader(const module_loader&) = delete;
        module_loader& operator=(const module_loader&) = delete;

        module_loader(module_loader&& other) noexcept
            : path_(std::move(other.path_)),
              registry_(other.registry_),
              handle_(std::exchange(other.handle_, nullptr)),
              modules_(std::move(other.modules_)) {}

        module_loader& operator=(module_loader&& other) noexcept {
            if (this != &other) {
                unload();
                path_ = std::move(other.path_);
                registry_ = other.registry_;
                handle_ = std::exchange(other.handle_, nullptr);
                modules_ = std::move(other.modules_);
            }
            return *this;
        }

        // Имена модулей, зарегистрированных этим плагином.
        [[nodiscard]] const std::vector<std::string>& modules() const { return modules_; }
        [[nodiscard]] const std::filesystem::path& path() const { return path_; }

    private:
        template <typename TFn>
        TFn* load_symbol(const char* name) const {
            void* symbol = detail::find_symbol(handle_, name);
            if (!symbol) {
                throw std::runtime_error("plugin '" + path_.string() +
                                         "' has no symbol '" + name + "'");
            }
            return reinterpret_cast<TFn*>(symbol);
        }

        void unload() noexcept {
            if (!handle_) {
                return;
            }
            // сначала фабрики — их vtable лежат в ещё загруженной библиотеке
            for (const auto& name : modules_) {
                registry_->remove(name);
            }
            modules_.clear();
            detail::close_library(handle_);
            handle_ = nullptr;
        }

        std::filesystem::path path_;
        module_registry* registry_;
        void* handle_ = nullptr;
        std::vector<std::string> modules_;
    };

} // namespace atp

#endif // ANITOOLSPLATFORM_MODULE_LOADER_HPP
