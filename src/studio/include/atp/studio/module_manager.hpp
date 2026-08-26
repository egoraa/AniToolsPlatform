// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_MODULE_MANAGER_HPP
#define ATP_STUDIO_MODULE_MANAGER_HPP

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <typeindex>
#include <utility>
#include <vector>

#include <atp/hosting/module_declaration.hpp>
#include <atp/hosting/module_registry.hpp>
#include <atp/io/input_base.hpp>
#include <atp/io/inputs.hpp>
#include <atp/io/output_base.hpp>
#include <atp/io/outputs.hpp>
#include <atp/io/properties.hpp>
#include <atp/io/property_base.hpp>
#include <atp/module/module_config.hpp>
#include <atp/runtime/module_loader.hpp>
#include <atp/runtime/raw_config.hpp>

namespace atp::studio {

/// Description of one declared port. The SDK's own type under studio's historical name: the fields
/// are the same two, and an alias keeps every consumer — the palette, the canvas, port_types and the
/// MCP catalog — spelling them as before.
using port_info = atp::port_declaration;

/// Description of one property: everything the inspector needs to pick a widget and to convert what
/// was typed back into JSON.
using property_info = atp::property_declaration;

/// Description of one registered module.
struct module_info {
    std::string name;
    version ver;
    std::vector<port_info> inputs;
    std::vector<port_info> outputs;
    std::vector<property_info> properties;
    bool broken = false;
    std::string error;

    /// File the module is declared in, empty unless the plugin said so — in practice the script of a
    /// Python module. Filled when the plugin is loaded, not by describe(): a factory does not know
    /// which file it came from, and the loader does.
    std::string source;

    /// Whether a module of this factory is handed a config at all — that is, whether it declared a
    /// constructor taking one.
    ///
    /// It is a separate answer from config_schema, and the pair is what tells "takes no config" apart
    /// from "takes one it does not describe". A module writing `using config_type = atp::module_config;`
    /// declares no field and reads text() instead, which is the whole point of a "file:" config; a C
    /// module and both bridges take the document whole. All three take a config and have no fields to
    /// show, and reporting them as a module that accepts nothing is how a config that would have
    /// reached them stops being written.
    bool takes_config = false;

    /// The config a module of this factory declares: structure and defaults, no values. Empty when
    /// the module declares none, and empty as well for a module that takes the document whole — a C
    /// module is still edited as text.
    ///
    /// A prototype object rather than a copied description, because the object already answers every
    /// question a description was invented to answer, and answers them from the one implementation the
    /// module itself was built against.
    std::shared_ptr<const atp::module_config> config_schema;
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
        const std::filesystem::path canonical =
            std::filesystem::weakly_canonical(runtime::detail::with_plugin_extension(file));
        plugin_info* existing = find_info(canonical);
        if (existing != nullptr && existing->loaded) {
            return;
        }
        plugin_info info{canonical, false, {}, {}};
        try {
            runtime::module_loader loader(canonical, registry_);
            for (const runtime::registered_module& m : loader.modules()) {
                module_info described = describe(registry_.at(m.name, m.ver));
                described.source = m.source;
                info.modules.push_back(std::move(described));
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

    /// Reads a plugin file again: the previous loader is dropped, which withdraws exactly the
    /// factories it registered, and the file is loaded from scratch. This is how a rebuilt plugin is
    /// picked up, and how the Python bridge is made to see a script written after it was loaded — the
    /// bridge scans for scripts when it loads and at no other moment.
    ///
    /// The old loader must die before the new one is made: registering a name that is still in the
    /// registry fails as a duplicate, and the loader would then withdraw the whole file. A library
    /// that is not pinned in the process is really unloaded here, which is what makes this correct
    /// for a rebuilt file as well.
    ///
    /// Whether a reload is allowed at all is the caller's decision: it unregisters factories, and
    /// doing that under a running pipeline would leave the tree holding modules whose factory is
    /// gone.
    /// @param file plugin path, canonical or not, extension optional
    /// @return false if this file was never loaded — there is nothing to reload
    bool reload_plugin(const std::filesystem::path& file) {
        const std::filesystem::path canonical =
            std::filesystem::weakly_canonical(runtime::detail::with_plugin_extension(file));
        if (find_info(canonical) == nullptr) {
            return false;
        }
        std::erase_if(loaders_, [&canonical](const runtime::module_loader& l) { return l.path() == canonical; });
        std::erase_if(plugins_, [&canonical](const plugin_info& p) { return p.path == canonical; });
        load_plugin(canonical);
        return true;
    }

    /// Reads every loaded plugin file again, in the order they were loaded.
    ///
    /// What a scan cannot do. `rescan` deliberately leaves a loaded plugin alone, so a file that
    /// changed on disk after its load is invisible to it — and for the Python bridge that is every
    /// edit of every script, since the scripts are read when the bridge loads and at no other moment.
    /// This is the operation behind a "refresh" gesture, where a person means the modules and not the
    /// directory listing.
    ///
    /// Paths are collected before the first reload because reloading rewrites the list. Files that
    /// failed to load are left to `rescan`, which retries them; a file that fails this time becomes a
    /// failed row like any other, which is the honest answer for a library that was deleted or is
    /// halfway through being rebuilt.
    ///
    /// Unregistering factories under a running pipeline would leave the tree holding modules whose
    /// factory is gone, and guarding against that is the caller's decision, exactly as in
    /// reload_plugin.
    void reload_all() {
        std::vector<std::filesystem::path> files;
        for (const plugin_info& p : plugins_) {
            if (p.loaded) {
                files.push_back(p.path);
            }
        }
        for (const std::filesystem::path& file : files) {
            (void)reload_plugin(file);
        }
    }

    /// Drops a loaded plugin: its loader dies, which withdraws exactly the factories it registered,
    /// and its row leaves the list as if the file had never been scanned.
    ///
    /// The counterpart of load_plugin, and the way a host undoes a load it should not have made — a
    /// second copy of a library whose statics only serve one instance, say. Modules already created
    /// from it stay alive: each pins the library through its deleter, so the code they run outlives
    /// the registration.
    /// @param file plugin path, canonical or not, extension optional
    /// @return false if this file was not among the plugins
    bool unload_plugin(const std::filesystem::path& file) {
        const std::filesystem::path canonical =
            std::filesystem::weakly_canonical(runtime::detail::with_plugin_extension(file));
        if (find_info(canonical) == nullptr) {
            return false;
        }
        std::erase_if(loaders_, [&canonical](const runtime::module_loader& l) { return l.path() == canonical; });
        std::erase_if(plugins_, [&canonical](const plugin_info& p) { return p.path == canonical; });
        return true;
    }

    /// Known plugin files, loaded and failed alike.
    [[nodiscard]] const std::vector<plugin_info>& plugins() const {
        return plugins_;
    }

    /// The file a loaded module was declared in — in practice the script behind a bridge — found by
    /// the name and version its factory answers to.
    ///
    /// The reverse of the lookup load_plugin does, and it has to exist because describe() cannot
    /// answer it: a factory does not know which file it came from, so the file is recorded on the
    /// plugin row and nowhere else. Anything holding a factory name rather than a plugin path — a
    /// project node, say — comes back through here.
    /// @param name registered module name
    /// @param ver the module's own version, as describe() reports it
    /// @return the file, empty when the plugin named none or no loaded plugin carries that module
    [[nodiscard]] std::string module_source(const std::string& name, const version& ver) const {
        for (const plugin_info& p : plugins_) {
            if (!p.loaded) {
                continue;
            }
            for (const module_info& m : p.modules) {
                if (m.name == name && m.ver == ver) {
                    return m.source;
                }
            }
        }
        return {};
    }

    /// Describes a module's ports and properties through its factory, which answers from the declared
    /// types and creates nothing.
    ///
    /// What this used to be, and why the change is visible from outside: a probe instance was built
    /// with an empty config and thrown away, so a constructor that threw made the module "broken" in
    /// the palette, and tolerating an empty config was something every module owed studio. Both are
    /// gone — a module is described by what it declares, not by whether one could be built.
    ///
    /// Broken is still reachable, and that is not a leftover: a module written by hand from
    /// module_base names no port node, so its factory falls back to probing, and a C module whose
    /// descriptor carries an unparsable default is refused here exactly as it would be at creation.
    ///
    /// A port list still cannot depend on a config. When it learns to, this is the call that has to
    /// learn where to get one.
    ///
    /// The config prototype is the factory's own object, and the config_ptr turns into a shared_ptr
    /// **with its deleter**: the pin that deleter carries is what keeps the plugin loaded for as long
    /// as the palette caches the prototype, and without it a rescan would leave a dangling vtable
    /// behind every entry it kept. A raw_config is deliberately not kept — it declares no field, so
    /// there would be nothing to show, and a module reading a document whole is edited as text. That
    /// it exists at all is recorded in takes_config instead, which is a different question and has to
    /// survive the prototype being dropped.
    [[nodiscard]] static module_info describe(const module_factory_base& factory) {
        module_info info{std::string(factory.name()), factory.get_version(), {}, {}, {}, false, {}, {}, false, {}};
        try {
            module_declaration decl = factory.declaration();
            info.inputs = std::move(decl.inputs);
            info.outputs = std::move(decl.outputs);
            info.properties = std::move(decl.properties);
            config_ptr declared = factory.make_config();
            info.takes_config = declared != nullptr;
            if (declared && dynamic_cast<const runtime::raw_config*>(declared.get()) == nullptr) {
                info.config_schema = std::shared_ptr<const atp::module_config>(std::move(declared));
            }
        } catch (const std::exception& e) {
            info.broken = true;
            info.error = e.what();
        }
        return info;
    }

   private:
    [[nodiscard]] static bool is_plugin_file(const std::filesystem::path& p) {
        return p.extension() == runtime::plugin_extension;
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
    std::vector<runtime::module_loader> loaders_;
    std::vector<plugin_info> plugins_;
    std::vector<std::filesystem::path> search_dirs_;
};

}  // namespace atp::studio

#endif
