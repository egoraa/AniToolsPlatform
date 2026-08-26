// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_RUNTIME_CONFIG_MODEL_HPP
#define ATP_RUNTIME_CONFIG_MODEL_HPP

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <atp/config/node.hpp>
#include <atp/config/read.hpp>
#include <atp/runtime/config_error.hpp>
#include <atp/runtime/pipeline_runner.hpp>
#include <atp/support/version.hpp>

namespace atp::runtime {

/// Config schema version the application understands: the config's major has to match and its minor
/// must not exceed ours, so that fields "from the future" are rejected rather than silently
/// ignored. The "version" field is the first thing checked.
///
/// 3.2 added a module's "config" and the document's "configs": a minor step, because keys are added
/// while the shape of the existing ones does not change, so a 3.1 document still reads unaltered.
///
/// 3.3 added the "file:" source of a config, allowed both on a module and as an entry of "configs" —
/// minor by the same argument, since no key changed shape and only which strings inside one are legal
/// did.
inline constexpr version config_schema_version{3, 3};

/// A plain module within a group.
///
/// `config` is the structured setting the module reads in its constructor, held **exactly as
/// written** — an object stays an object, and a reference or a "file:" path stays that very string.
/// Expanding either on the way out is not allowed, and keeping the original spelling is the cheapest
/// way of never doing it: encode becomes a copy of the node and encode(decode(doc)) == doc needs no
/// logic of its own. It is also what makes a file reference survive a save: a document that named a
/// file still names it afterwards instead of having swallowed its contents. Resolving a reference,
/// reading a file and converting to a raw_config all happen in the builder instead.
struct module_node {
    std::string factory;
    std::string name;
    std::optional<version> factory_version;
    std::vector<std::pair<std::string, atp::config::node>> properties;
    std::optional<atp::config::node> config;
};

struct group_node;

/// An entry of a group's "modules" list — a subgroup being a module too, exactly one of the fields
/// is filled in (a decode invariant). The unique_ptr is there for type recursion; the order within
/// the vector is significant — it is the insertion order into the group, hence the order of the
/// lifecycle cascades.
struct child_node {
    std::optional<module_node> module;
    std::unique_ptr<group_node> group;
};

/// A connection within a group's scope.
struct connection_node {
    std::string from;
    std::string to;
};

/// A group with its modules (subgroups among them), exported ports and connections.
struct group_node {
    std::string name;
    std::vector<child_node> modules;
    std::vector<std::pair<std::string, std::string>> expose_inputs;
    std::vector<std::pair<std::string, std::string>> expose_outputs;
    std::vector<connection_node> connections;
};

/// A runner thread declaration.
struct thread_node {
    std::string name;
    thread_mode mode = thread_mode::on_demand;
    std::chrono::milliseconds period{};
};

/// A whole config document in typed form.
///
/// `configs` holds the shared blocks a module's "config" may name instead of spelling one out, stored
/// verbatim for the same reason module_node::config is. An entry is required to be an object or a
/// "file:" path — never a bare reference to another entry — so that the root of a config is an object
/// (or null) whichever spelling was used, a module never has to write code about which one the file
/// chose, and a chain of references is at most one step long, which is what leaves no cycle to guard
/// against.
struct config {
    version schema;
    std::vector<std::string> plugins;
    group_node pipeline;
    std::vector<thread_node> threads;
    std::vector<std::pair<std::string, std::string>> assignments;
    std::vector<std::pair<std::string, atp::config::node>> configs;
};

/// Splits a module's "config" string into the source it names and the entry within it.
///
/// A string is always a reference, never a literal — a config that is itself a string is written as
/// {"value": "…"} — and it is parsed on the **first** colon, the same move parse_property_override
/// makes when it splits "path.prop=value" on the first '='. No prefix means the default source, the
/// document's own "configs" block, and the entry name is returned. A prefix means some other source,
/// which this function deliberately does not decode — it only says "not the default one", so a caller
/// that knows the sources ("file:" is the one there is) matches on the prefix itself.
///
/// @return the entry name for a string without a prefix, nullopt for a string carrying one
[[nodiscard]] inline std::optional<std::string> parse_config_ref(std::string_view text) {
    const std::size_t colon = text.find(':');
    if (colon == std::string_view::npos) {
        return std::string(text);
    }
    return std::nullopt;
}

namespace detail {

inline module_node decode_module(const atp::config::node& j) {
    module_node m;
    m.factory = j.string_at("module");
    m.name = atp::config::string_or(j.find("name"), m.factory);
    if (const atp::config::node* declared = j.find("version"); declared != nullptr) {
        const std::optional<version> v = try_parse_version(declared->as_string());
        if (!v) {
            throw std::logic_error("decode after validate: bad version");
        }
        m.factory_version = *v;
    }
    if (const atp::config::node* props = j.find("properties"); props != nullptr) {
        for (const auto& [name, value] : props->entries()) {
            m.properties.emplace_back(name, value);
        }
    }
    if (const atp::config::node* cfg = j.find("config"); cfg != nullptr) {
        m.config = *cfg;
    }
    return m;
}

inline group_node decode_group(std::string name, const atp::config::node& j);

inline child_node decode_child(const atp::config::node& j) {
    child_node c;
    if (j.find("module") != nullptr) {
        c.module = decode_module(j);
    } else {
        c.group = std::make_unique<group_node>(decode_group(j.string_at("group"), j));
    }
    return c;
}

inline void decode_expose(const atp::config::node* map, std::vector<std::pair<std::string, std::string>>& into) {
    if (map == nullptr) {
        return;
    }
    for (const auto& [alias, path] : map->entries()) {
        into.emplace_back(alias, path.as_string());
    }
}

inline group_node decode_group(std::string name, const atp::config::node& j) {
    group_node g;
    g.name = std::move(name);
    if (const atp::config::node* modules = j.find("modules"); modules != nullptr) {
        for (const atp::config::node& child : modules->elements()) {
            g.modules.push_back(decode_child(child));
        }
    }
    if (const atp::config::node* expose = j.find("expose"); expose != nullptr) {
        decode_expose(expose->find("inputs"), g.expose_inputs);
        decode_expose(expose->find("outputs"), g.expose_outputs);
    }
    if (const atp::config::node* connections = j.find("connections"); connections != nullptr) {
        for (const atp::config::node& c : connections->elements()) {
            g.connections.push_back({c.string_at("from"), c.string_at("to")});
        }
    }
    return g;
}

inline atp::config::node encode_group_body(const group_node& g);

inline atp::config::node encode_child(const child_node& c) {
    if (c.module) {
        atp::config::node j;
        j["module"] = atp::config::node(c.module->factory);
        if (c.module->name != c.module->factory) {
            j["name"] = atp::config::node(c.module->name);
        }
        if (c.module->factory_version) {
            j["version"] = atp::config::node(c.module->factory_version->to_string());
        }
        if (!c.module->properties.empty()) {
            atp::config::node props(atp::config::node::object_type{});
            for (const auto& [name, value] : c.module->properties) {
                props[name] = value;
            }
            j["properties"] = std::move(props);
        }
        if (c.module->config) {
            j["config"] = *c.module->config;
        }
        return j;
    }
    atp::config::node j = encode_group_body(*c.group);
    j["group"] = atp::config::node(c.group->name);
    return j;
}

inline atp::config::node encode_group_body(const group_node& g) {
    atp::config::node j(atp::config::node::object_type{});
    if (!g.modules.empty()) {
        atp::config::node modules(atp::config::node::array_type{});
        for (const child_node& c : g.modules) {
            modules.push_back(encode_child(c));
        }
        j["modules"] = std::move(modules);
    }
    if (!g.expose_inputs.empty() || !g.expose_outputs.empty()) {
        atp::config::node expose(atp::config::node::object_type{});
        if (!g.expose_inputs.empty()) {
            atp::config::node inputs(atp::config::node::object_type{});
            for (const auto& [alias, path] : g.expose_inputs) {
                inputs[alias] = atp::config::node(path);
            }
            expose["inputs"] = std::move(inputs);
        }
        if (!g.expose_outputs.empty()) {
            atp::config::node outputs(atp::config::node::object_type{});
            for (const auto& [alias, path] : g.expose_outputs) {
                outputs[alias] = atp::config::node(path);
            }
            expose["outputs"] = std::move(outputs);
        }
        j["expose"] = std::move(expose);
    }
    if (!g.connections.empty()) {
        atp::config::node connections(atp::config::node::array_type{});
        for (const connection_node& c : g.connections) {
            connections.push_back(atp::config::node::object({{"from", c.from}, {"to", c.to}}));
        }
        j["connections"] = std::move(connections);
    }
    return j;
}

}  // namespace detail

/// Converts a document into the typed model.
/// @param doc document that has already passed validate() — that is the contract
/// @throws std::logic_error if the document turns out not to match the schema after all
[[nodiscard]] inline config decode(const atp::config::node& doc) {
    config cfg;
    const std::optional<version> schema = try_parse_version(doc.string_at("version"));
    if (!schema) {
        throw std::logic_error("decode after validate: bad schema version");
    }
    cfg.schema = *schema;
    if (const atp::config::node* plugins = doc.find("plugins"); plugins != nullptr) {
        for (const atp::config::node& p : plugins->elements()) {
            cfg.plugins.push_back(p.as_string());
        }
    }
    cfg.pipeline = detail::decode_group("root", doc.at("pipeline"));
    if (const atp::config::node* threads = doc.find("threads"); threads != nullptr) {
        for (const atp::config::node& t : threads->elements()) {
            thread_node n;
            n.name = t.string_at("name");
            const std::string mode = atp::config::string_or(t.find("mode"), "on_demand");
            if (mode == "throttled") {
                n.mode = thread_mode::throttled;
                n.period = std::chrono::milliseconds(t.int_at("period_ms"));
            } else if (mode == "spinning") {
                n.mode = thread_mode::spinning;
            }
            cfg.threads.push_back(std::move(n));
        }
    }
    if (const atp::config::node* assign = doc.find("assign"); assign != nullptr) {
        for (const auto& [path, thread] : assign->entries()) {
            cfg.assignments.emplace_back(path, thread.as_string());
        }
    }
    if (const atp::config::node* configs = doc.find("configs"); configs != nullptr) {
        for (const auto& [name, value] : configs->entries()) {
            cfg.configs.emplace_back(name, value);
        }
    }
    return cfg;
}

/// Converts the model back into a document, the inverse of decode. Produces the canonical form:
/// defaults omitted, the thread mode spelled out, property values written as scalars of their own
/// type. The invariants are that validate(encode(cfg)) is empty and, for a canonical document,
/// json_dump(encode(decode(doc))) == json_dump(doc).
///
/// The round trip is pinned on the dumped text rather than on the tree, and that is not a weaker
/// claim: an object of config::node keeps the order it was given, while this function writes keys in
/// the order the code below happens to run, so two equal documents may hold their keys differently.
/// json_dump sorts them, which is the form every document here was already saved in. The order of
/// expose aliases does not survive a round trip for the same reason, and that does not affect the
/// meaning of the config.
[[nodiscard]] inline atp::config::node encode(const config& cfg) {
    atp::config::node doc(atp::config::node::object_type{});
    doc["version"] = atp::config::node(cfg.schema.to_string());
    if (!cfg.plugins.empty()) {
        atp::config::node plugins(atp::config::node::array_type{});
        for (const std::string& p : cfg.plugins) {
            plugins.push_back(atp::config::node(p));
        }
        doc["plugins"] = std::move(plugins);
    }
    doc["pipeline"] = detail::encode_group_body(cfg.pipeline);
    if (!cfg.threads.empty()) {
        atp::config::node threads(atp::config::node::array_type{});
        for (const thread_node& t : cfg.threads) {
            atp::config::node tj;
            tj["name"] = atp::config::node(t.name);
            switch (t.mode) {
                case thread_mode::on_demand:
                    tj["mode"] = atp::config::node("on_demand");
                    break;
                case thread_mode::throttled:
                    tj["mode"] = atp::config::node("throttled");
                    tj["period_ms"] = atp::config::node(static_cast<std::int64_t>(t.period.count()));
                    break;
                case thread_mode::spinning:
                    tj["mode"] = atp::config::node("spinning");
                    break;
            }
            threads.push_back(std::move(tj));
        }
        doc["threads"] = std::move(threads);
    }
    if (!cfg.assignments.empty()) {
        atp::config::node assign(atp::config::node::object_type{});
        for (const auto& [path, thread] : cfg.assignments) {
            assign[path] = atp::config::node(thread);
        }
        doc["assign"] = std::move(assign);
    }
    if (!cfg.configs.empty()) {
        atp::config::node configs(atp::config::node::object_type{});
        for (const auto& [name, value] : cfg.configs) {
            configs[name] = value;
        }
        doc["configs"] = std::move(configs);
    }
    return doc;
}

}  // namespace atp::runtime

#endif
