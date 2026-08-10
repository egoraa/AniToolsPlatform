// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_RUNTIME_PIPELINE_BUILDER_HPP
#define ATP_RUNTIME_PIPELINE_BUILDER_HPP

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <atp/group.hpp>
#include <atp/module_loader.hpp>
#include <atp/module_registry.hpp>
#include <atp/pipeline.hpp>
#include <atp/pipeline_runner.hpp>
#include <atp/runtime/config_model.hpp>
#include <atp/runtime/config_value_json.hpp>

namespace atp::runtime {

/// An assembled application. Member order is destruction order reversed: the runner dies first
/// (stopping the threads while still referencing the pipeline), the pipeline before the loaders
/// (modules pin their own DLLs), and the registry last (the loaders withdraw their factories from
/// it).
struct application {
    module_registry registry;
    std::vector<module_loader> plugins;
    pipeline pipe;
    pipeline_runner runner;

    application() = default;
    application(const application&) = delete;
    application& operator=(const application&) = delete;
};

namespace detail {

inline std::string scalar_to_string(const nlohmann::json& value) {
    if (value.is_string()) {
        return value.get<std::string>();
    }
    if (value.is_boolean()) {
        return value.get<bool>() ? "true" : "false";
    }
    return value.dump();
}

inline void apply_properties(module_base& m, const module_node& node) {
    for (const auto& [name, value] : node.properties) {
        io::property_base* prop = m.properties().find(name);
        if (prop == nullptr) {
            throw config_error("no property named '" + name + "'");
        }
        try {
            prop->from_string(scalar_to_string(value));
        } catch (const std::invalid_argument& e) {
            throw config_error(e.what());
        }
    }
}

/// Turns a module node's config into the value handed to its constructor, following a reference into
/// the document's shared block.
///
/// Resolution lives here rather than in the model because the model holds what was written verbatim,
/// so that a round trip costs nothing and a reference is never expanded on save — which means it is
/// not the model's business to expand one at all. The two throws below look redundant next to the
/// validator, which rejects both cases earlier, but the builder is also called from studio, where a
/// document never passed validate(); on that path these are the only checks there are.
///
/// @throws config_error for an unsupported source prefix or a name absent from the shared block
[[nodiscard]] inline config_value resolve_config(const module_node& node,
                                                 const std::vector<std::pair<std::string, nlohmann::json>>& shared) {
    if (!node.config) {
        return config_value{};
    }
    if (!node.config->is_string()) {
        return to_config_value(*node.config);
    }
    const std::string text = node.config->get<std::string>();
    const std::optional<std::string> ref = parse_config_ref(text);
    if (!ref) {
        throw config_error("unknown config source in '" + text + "'");
    }
    for (const auto& [name, value] : shared) {
        if (name == *ref) {
            return to_config_value(value);
        }
    }
    throw config_error("no entry named '" + *ref + "' in 'configs'");
}

inline void build_group(group& g,
                        const group_node& node,
                        module_registry& registry,
                        const std::vector<std::pair<std::string, nlohmann::json>>& shared) {
    for (const child_node& c : node.modules) {
        if (c.module) {
            try {
                const config_value cfg = resolve_config(*c.module, shared);
                module_ptr m = c.module->factory_version
                                   ? registry.create(c.module->factory, *c.module->factory_version, cfg)
                                   : registry.create(c.module->factory, cfg);
                apply_properties(*m, *c.module);
                g.add(c.module->name, std::move(m));
            } catch (const std::exception& e) {
                throw config_error("module '" + c.module->name + "' in group '" + std::string(g.get_name()) +
                                   "': " + e.what());
            }
        } else {
            build_group(g.add_group(c.group->name), *c.group, registry, shared);
        }
    }
    for (const auto& [alias, path] : node.expose_inputs) {
        try {
            g.expose_input(alias, path);
        } catch (const std::exception& e) {
            throw config_error("expose input '" + alias + "' in group '" + std::string(g.get_name()) +
                               "': " + e.what());
        }
    }
    for (const auto& [alias, path] : node.expose_outputs) {
        try {
            g.expose_output(alias, path);
        } catch (const std::exception& e) {
            throw config_error("expose output '" + alias + "' in group '" + std::string(g.get_name()) +
                               "': " + e.what());
        }
    }
    for (const connection_node& c : node.connections) {
        try {
            g.connect(c.from, c.to);
        } catch (const std::exception& e) {
            throw config_error("connection '" + c.from + "' -> '" + c.to + "' in group '" + std::string(g.get_name()) +
                               "': " + e.what());
        }
    }
}

inline group& resolve_group(group& root, const std::string& path) {
    group* current = &root;
    std::size_t begin = 0;
    while (true) {
        const std::size_t dot = path.find('.', begin);
        const std::string segment = path.substr(begin, dot == std::string::npos ? dot : dot - begin);
        group* next = current->find_group(segment);
        if (!next) {
            throw config_error("assign: group path '" + path + "' not found in pipeline");
        }
        current = next;
        if (dot == std::string::npos) {
            return *current;
        }
        begin = dot + 1;
    }
}

/// Plugin files directly inside a directory, sorted by name. Not recursive, matching studio's own
/// scan, so that one directory yields the same set in both places. Sorted because directory_iterator
/// promises no order and a host's startup has to be reproducible: the order decides which plugin
/// claims a module name first, and therefore which of two conflicting ones is the error.
[[nodiscard]] inline std::vector<std::filesystem::path> plugin_files_in(const std::filesystem::path& dir) {
    std::vector<std::filesystem::path> files;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == plugin_extension) {
            files.push_back(entry.path());
        }
    }
    std::ranges::sort(files);
    return files;
}

}  // namespace detail

/// Builds the tree, the threads and the layout against an already populated registry — studio's
/// path, where the plugins live at session level and the pipeline is rebuilt for every run.
/// @throws config_error with the config context of whatever failed to build
inline void build_pipeline(pipeline& pipe, pipeline_runner& runner, const config& cfg, module_registry& registry) {
    detail::build_group(pipe.root(), cfg.pipeline, registry, cfg.configs);
    for (const thread_node& t : cfg.threads) {
        runner.add_thread(t.name, {t.mode, t.period});
    }
    for (const auto& [path, thread] : cfg.assignments) {
        runner.assign(detail::resolve_group(pipe.root(), path), thread);
    }
}

/// Builds an application from the model: plugins → the tree of groups and modules → threads → the
/// layout. The runner is NOT started; starting and stopping stay with the caller.
/// @param app application to fill in: its registry receives the plugins, its pipeline the tree
/// @param cfg decoded config to build from
/// @param base_dir directory of the root config, which plugin paths are resolved against
/// @throws config_error with the config context of whatever failed to build
/// @throws std::runtime_error naming the path if a plugin fails to load
///
/// An entry naming a directory loads the plugins found directly in it. A file there that is not a
/// plugin at all is skipped — a directory is "whatever is in it", and a foreign library lying next to
/// the plugins must not stop a host. Every other failure still stops it, including a file that would
/// not open: that never got as far as the entry points, so calling it foreign would hide the most
/// common real breakage. An entry naming a file keeps failing on anything at all, because there the
/// config promised that this particular plugin is there.
inline void build(application& app, const config& cfg, const std::filesystem::path& base_dir) {
    std::vector<std::filesystem::path> loaded;
    for (const std::string& p : cfg.plugins) {
        const std::filesystem::path entry = base_dir / p;
        const bool is_dir = std::filesystem::is_directory(entry);
        for (const std::filesystem::path& file :
             is_dir ? detail::plugin_files_in(entry) : std::vector<std::filesystem::path>{entry}) {
            const std::filesystem::path canonical =
                std::filesystem::weakly_canonical(atp::detail::with_plugin_extension(file));
            if (std::ranges::find(loaded, canonical) != loaded.end()) {
                continue;
            }
            try {
                app.plugins.emplace_back(canonical, app.registry);
            } catch (const not_a_plugin&) {
                if (!is_dir) {
                    throw;
                }
                continue;
            }
            loaded.push_back(canonical);
        }
    }
    build_pipeline(app.pipe, app.runner, cfg, app.registry);
}

}  // namespace atp::runtime

#endif
