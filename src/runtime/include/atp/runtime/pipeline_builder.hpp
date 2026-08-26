// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_RUNTIME_PIPELINE_BUILDER_HPP
#define ATP_RUNTIME_PIPELINE_BUILDER_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <atp/config/node.hpp>
#include <atp/hosting/module_registry.hpp>
#include <atp/io/property_codec.hpp>
#include <atp/runtime/config_binding.hpp>
#include <atp/runtime/config_file.hpp>
#include <atp/runtime/config_model.hpp>
#include <atp/runtime/config_source.hpp>
#include <atp/runtime/group.hpp>
#include <atp/runtime/module_loader.hpp>
#include <atp/runtime/pipeline.hpp>
#include <atp/runtime/pipeline_runner.hpp>

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

/// A real spelled so that it stays a real: with a trailing ".0" whenever the shortest round-trip form
/// carries neither a point nor an exponent.
///
/// That tail is **load-bearing and not decoration**. std::to_chars prints 48000.0 as "48000", and
/// property_codec<int> parses "48000" happily — so without it a real written in the config would
/// silently satisfy an integer property, which is the very distinction config::node keeps two forms
/// for. With it, from_chars stops at the point and the property refuses, as it always did.
///
/// It is also exactly what the JSON writer this replaced produced, checked against nlohmann's dump
/// over 48000.0, 3.0, -0.0, 0.1, 0.5, 1e20, 1e-7, 1/3 and 123456789012345.0. Infinities and NaN need
/// no case of their own: to_chars writes "inf" and "nan", the tail makes them "inf.0" and "nan.0", and
/// every property refuses those — which is what the JSON writer's "null" achieved.
[[nodiscard]] inline std::string real_to_string(double value) {
    std::string text = io::property_codec<double>::to_string(value);
    if (text.find_first_of(".eE") == std::string::npos) {
        text += ".0";
    }
    return text;
}

/// The string form of a property value as the config declares it, handed straight to
/// property_base::from_string.
///
/// Formatted here rather than by json_dump, which built a whole document tree to print one number. The
/// spelling is unchanged, deliberately: it decides which properties accept the value, so this is a
/// change of layer and not of meaning.
///
/// @return nullopt for a value that is not a scalar, which only the caller can report, being the one
///         that knows the property's name
[[nodiscard]] inline std::optional<std::string> scalar_to_string(const atp::config::node& value) {
    switch (value.kind()) {
        case atp::config::kind::string:
            return value.as_string();
        case atp::config::kind::boolean:
            return io::property_codec<bool>::to_string(value.as_bool());
        case atp::config::kind::integer:
            return io::property_codec<std::int64_t>::to_string(value.as_int());
        case atp::config::kind::real:
            return real_to_string(value.as_double());
        case atp::config::kind::null:
        case atp::config::kind::array:
        case atp::config::kind::object:
            break;
    }
    return std::nullopt;
}

inline void apply_properties(module_base& m, const module_node& node) {
    for (const auto& [name, value] : node.properties) {
        io::property_base* prop = m.properties().find(name);
        if (prop == nullptr) {
            throw config_error("no property named '" + name + "'");
        }
        const std::optional<std::string> text = scalar_to_string(value);
        if (!text) {
            throw config_error("property '" + name + "' must be a scalar, found " +
                               std::string(atp::config::node::kind_name(value.kind())));
        }
        try {
            prop->from_string(*text);
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
/// A shared entry may itself be a "file:" path, which is how one file serves several modules. It may
/// not be a bare reference to another entry — the validator refuses that — so this follows at most one
/// reference and one file, and there is no cycle to detect.
///
/// @param base_dir directory of the document, which a relative "file:" path resolves against
/// @throws config_error for an unsupported source prefix, a name absent from the shared block, or
///         anything that goes wrong reading a named file
[[nodiscard]] inline config_source resolve_config(const module_node& node,
                                                  const std::vector<std::pair<std::string, atp::config::node>>& shared,
                                                  const std::filesystem::path& base_dir) {
    if (!node.config) {
        return config_source{};
    }
    if (!node.config->is_string()) {
        return config_source{*node.config, {}, {}, false};
    }
    const std::string text = node.config->as_string();
    if (text.starts_with(config_file_prefix)) {
        return load_config_source(std::string_view(text).substr(config_file_prefix.size()), base_dir);
    }
    const std::optional<std::string> ref = parse_config_ref(text);
    if (!ref) {
        throw config_error("unknown config source in '" + text + "'");
    }
    for (const auto& [name, value] : shared) {
        if (name == *ref) {
            if (value.is_string()) {
                const std::string shared_text = value.as_string();
                if (!shared_text.starts_with(config_file_prefix)) {
                    throw config_error("config '" + *ref + "' in 'configs' must be an object or a 'file:' path");
                }
                return load_config_source(std::string_view(shared_text).substr(config_file_prefix.size()), base_dir);
            }
            return config_source{value, {}, {}, false};
        }
    }
    throw config_error("no entry named '" + *ref + "' in 'configs'");
}

inline void build_group(group& g,
                        const group_node& node,
                        module_registry& registry,
                        const std::vector<std::pair<std::string, atp::config::node>>& shared,
                        const std::filesystem::path& base_dir) {
    for (const child_node& c : node.modules) {
        if (c.module) {
            try {
                const config_source src = resolve_config(*c.module, shared, base_dir);
                const module_factory_base& factory = c.module->factory_version
                                                         ? registry.at(c.module->factory, *c.module->factory_version)
                                                         : registry.at(c.module->factory);
                config_ptr cfg = factory.make_config();
                if (cfg) {
                    load_fields_or_throw(*cfg, src);
                }
                module_ptr m = factory.create(std::move(cfg));
                apply_properties(*m, *c.module);
                g.add(c.module->name, std::move(m));
            } catch (const std::exception& e) {
                throw config_error("module '" + c.module->name + "' in group '" + std::string(g.get_name()) +
                                   "': " + e.what());
            }
        } else {
            build_group(g.add_group(c.group->name), *c.group, registry, shared, base_dir);
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
///
/// @param base_dir directory of the document being built, which a module config written as
///        "file:<path>" resolves against. Optional because most callers have no document on disk at
///        all — a config assembled in memory, a test, a project that was never saved — and for them a
///        relative file config is refused by name rather than resolved against whatever the process
///        happens to have as its current directory.
/// @throws config_error with the config context of whatever failed to build
inline void build_pipeline(pipeline& pipe,
                           pipeline_runner& runner,
                           const config& cfg,
                           module_registry& registry,
                           const std::filesystem::path& base_dir = {}) {
    detail::build_group(pipe.root(), cfg.pipeline, registry, cfg.configs, base_dir);
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
/// @param base_dir directory of the root config, which both plugin paths and a module config written
///        as "file:<path>" are resolved against
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
                std::filesystem::weakly_canonical(detail::with_plugin_extension(file));
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
    build_pipeline(app.pipe, app.runner, cfg, app.registry, base_dir);
}

}  // namespace atp::runtime

#endif
