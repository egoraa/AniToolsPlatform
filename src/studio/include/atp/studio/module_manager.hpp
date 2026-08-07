// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_MODULE_MANAGER_HPP
#define ATP_STUDIO_MODULE_MANAGER_HPP

#include <algorithm>
#include <filesystem>
#include <string>
#include <typeindex>
#include <utility>
#include <vector>

#include <atp/io/input_base.hpp>
#include <atp/io/inputs.hpp>
#include <atp/io/output_base.hpp>
#include <atp/io/outputs.hpp>
#include <atp/io/properties.hpp>
#include <atp/io/property_base.hpp>
#include <atp/module_loader.hpp>
#include <atp/module_registry.hpp>

namespace atp::studio {

/// Description of one declared port.
struct port_info {
    std::string name;
    std::type_index type;
};

/// Description of one property: everything the inspector needs to pick a widget and to convert what
/// was typed back into JSON. The option set is copied, so the description outlives the temporary
/// probe module it was taken from.
struct property_info {
    std::string name;
    io::property_kind kind;
    std::string default_value;
    std::vector<std::string> options;
    bool persistent = true;
};

/// Description of one registered module.
struct module_info {
    std::string name;
    version ver;
    std::vector<port_info> inputs;
    std::vector<port_info> outputs;
    std::vector<property_info> properties;
    bool broken = false;
    std::string error;
};

/// Description of one plugin file and the modules it brought.
struct plugin_info {
    std::filesystem::path path;
    bool loaded = false;
    std::string error;
    std::vector<module_info> modules;
};

/// Session-level owner of the module registry and the DLL loaders; studio's palette is its shop
/// window. Search directories are chosen by the user, and config plugins are loaded through the
/// same paths.
class module_manager {
   public:
    /// Registry the loaded factories live in.
    [[nodiscard]] module_registry& registry() {
        return registry_;
    }

    /// Adds a plugin search directory, ignoring duplicates.
    void add_search_dir(std::filesystem::path dir) {
        if (std::ranges::find(search_dirs_, dir) == search_dirs_.end()) {
            search_dirs_.push_back(std::move(dir));
        }
    }

    /// Removes a search directory. The DLLs already loaded from it stay: an open project may
    /// reference their modules, and unloading from under it is worse than a stale entry.
    /// @return false if the directory was not in the list
    bool remove_search_dir(const std::filesystem::path& dir) {
        return std::erase(search_dirs_, dir) > 0;
    }

    /// Current plugin search directories.
    [[nodiscard]] const std::vector<std::filesystem::path>& search_dirs() const {
        return search_dirs_;
    }

    /// Scans the search directories: new files are loaded and earlier failures are retried (the
    /// file may have been rebuilt), while successfully loaded plugins are left alone.
    void rescan() {
        for (const std::filesystem::path& dir : search_dirs_) {
            std::error_code ec;
            for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
                if (entry.is_regular_file() && is_plugin_file(entry.path())) {
                    load_plugin(entry.path());
                }
            }
        }
    }

    /// Loads one plugin file explicitly, the way config plugins arrive. An already loaded path is a
    /// no-op; a path that failed before is retried and its entry updated. Failures are recorded in
    /// the plugin_info rather than thrown.
    void load_plugin(const std::filesystem::path& file) {
        const std::filesystem::path canonical = std::filesystem::weakly_canonical(file);
        plugin_info* existing = find_info(canonical);
        if (existing != nullptr && existing->loaded) {
            return;
        }
        plugin_info info{canonical, false, {}, {}};
        try {
            module_loader loader(canonical, registry_);
            for (const auto& [name, ver] : loader.modules()) {
                info.modules.push_back(describe(registry_.at(name, ver)));
            }
            info.loaded = true;
            loaders_.push_back(std::move(loader));
        } catch (const std::exception& e) {
            info.error = e.what();
        }
        if (existing != nullptr) {
            *existing = std::move(info);
        } else {
            plugins_.push_back(std::move(info));
        }
    }

    /// Known plugin files, loaded and failed alike.
    [[nodiscard]] const std::vector<plugin_info>& plugins() const {
        return plugins_;
    }

    /// Describes a module's ports and properties through a probe instance: create it, enumerate the
    /// owned entries and throw it away. This leans on the lifecycle contract — the constructor is
    /// cheap and the heavy work happens in initialize, which is never called here. A throwing
    /// constructor makes the module "broken", and the palette shows why.
    [[nodiscard]] static module_info describe(const module_factory_base& factory) {
        module_info info{std::string(factory.name()), factory.get_version(), {}, {}, {}, false, {}};
        try {
            const module_ptr probe = factory.create();
            for (io::input_base* p : probe->inputs().owned()) {
                info.inputs.push_back({p->name(), p->type()});
            }
            for (io::output_base* p : probe->outputs().owned()) {
                info.outputs.push_back({p->name(), p->type()});
            }
            for (io::property_base* p : probe->properties().owned()) {
                info.properties.push_back({p->name(), p->kind(), p->default_string(), p->options(), p->persistent()});
            }
        } catch (const std::exception& e) {
            info.broken = true;
            info.error = e.what();
        }
        return info;
    }

   private:
    [[nodiscard]] static bool is_plugin_file(const std::filesystem::path& p) {
        return p.extension() == plugin_extension;
    }

    [[nodiscard]] plugin_info* find_info(const std::filesystem::path& canonical) {
        for (plugin_info& p : plugins_) {
            if (p.path == canonical) {
                return &p;
            }
        }
        return nullptr;
    }

    module_registry registry_;
    std::vector<module_loader> loaders_;
    std::vector<plugin_info> plugins_;
    std::vector<std::filesystem::path> search_dirs_;
};

}  // namespace atp::studio

#endif
