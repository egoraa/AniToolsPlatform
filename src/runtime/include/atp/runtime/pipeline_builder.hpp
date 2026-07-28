#ifndef ATP_RUNTIME_PIPELINE_BUILDER_HPP
#define ATP_RUNTIME_PIPELINE_BUILDER_HPP

#include <cstddef>
#include <filesystem>
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

// JSON scalar → the string property_base::from_string expects: a string as is (dump would add
// quotes), a bool spelled out (the codec's own form) and a number through dump (canonical text).
// The symmetry of the reverse path is studio's business, via the property kind.
inline std::string scalar_to_string(const nlohmann::json& value) {
    if (value.is_string()) {
        return value.get<std::string>();
    }
    if (value.is_boolean()) {
        return value.get<bool>() ? "true" : "false";
    }
    return value.dump();
}

// Config values are applied before g.add and initialize, so a module already sees its settings in
// initialize. An unknown name and an unparsable value are both config errors; the module context is
// added by the caller. The property's invalid_argument is translated into config_error as is — its
// text already names the property and, for an enum, lists the allowed names.
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

// Platform errors are wrapped in config context, so the user sees which node was being built and
// not just "no module named X".
inline void build_group(group& g, const group_node& node, module_registry& registry) {
    for (const child_node& c : node.modules) {
        if (c.module) {
            try {
                module_ptr m = c.module->factory_version
                                   ? registry.create(c.module->factory, *c.module->factory_version)
                                   : registry.create(c.module->factory);
                apply_properties(*m, *c.module);
                g.add(c.module->name, std::move(m));
            } catch (const std::exception& e) {
                throw config_error("module '" + c.module->name + "' in group '" + std::string(g.get_name()) +
                                   "': " + e.what());
            }
        } else {
            build_group(g.add_group(c.group->name), *c.group, registry);
        }
    }
    // Exports come after the modules and connections after the exports: connection paths may
    // reference subgroup aliases, which are ready by then.
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
            if (c.replay) {
                g.connect(c.from, c.to, io::replay);
            } else {
                g.connect(c.from, c.to);
            }
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

}  // namespace detail

/// Builds the tree, the threads and the layout against an already populated registry — studio's
/// path, where the plugins live at session level and the pipeline is rebuilt for every run.
/// @throws config_error with the config context of whatever failed to build
inline void build_pipeline(pipeline& pipe, pipeline_runner& runner, const config& cfg, module_registry& registry) {
    detail::build_group(pipe.root(), cfg.pipeline, registry);
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
inline void build(application& app, const config& cfg, const std::filesystem::path& base_dir) {
    for (const std::string& p : cfg.plugins) {
        app.plugins.emplace_back(base_dir / p, app.registry);
    }
    build_pipeline(app.pipe, app.runner, cfg, app.registry);
}

}  // namespace atp::runtime

#endif  // ATP_RUNTIME_PIPELINE_BUILDER_HPP
