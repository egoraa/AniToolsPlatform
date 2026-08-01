#ifndef ATP_RUNTIME_CONFIG_MODEL_HPP
#define ATP_RUNTIME_CONFIG_MODEL_HPP

#include <chrono>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <atp/pipeline_runner.hpp>
#include <atp/version.hpp>

namespace atp::runtime {

/// Config schema version the application understands: the config's major has to match and its minor
/// must not exceed ours, so that fields "from the future" are rejected rather than silently
/// ignored. The "version" field is the first thing checked.
inline constexpr version config_schema_version{2, 0};

/// Application-level error: reading, includes, building from a config.
class config_error : public std::runtime_error {
   public:
    using std::runtime_error::runtime_error;
};

/// A plain module within a group.
struct module_node {
    std::string factory;
    std::string name;
    std::optional<version> factory_version;
    std::vector<std::pair<std::string, nlohmann::json>> properties;
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
    bool replay = false;
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
struct config {
    version schema;
    std::vector<std::string> plugins;
    group_node pipeline;
    std::vector<thread_node> threads;
    std::vector<std::pair<std::string, std::string>> assignments;
};

namespace detail {

inline module_node decode_module(const nlohmann::json& j) {
    module_node m;
    m.factory = j.at("module").get<std::string>();
    m.name = j.value("name", m.factory);
    if (j.contains("version")) {
        const std::optional<version> v = try_parse_version(j.at("version").get<std::string>());
        if (!v) {
            throw std::logic_error("decode after validate: bad version");
        }
        m.factory_version = *v;
    }
    if (j.contains("properties")) {
        const nlohmann::json props = j.at("properties");
        for (const auto& [name, value] : props.items()) {
            m.properties.emplace_back(name, value);
        }
    }
    return m;
}

inline group_node decode_group(std::string name, const nlohmann::json& j);

inline child_node decode_child(const nlohmann::json& j) {
    child_node c;
    if (j.contains("module")) {
        c.module = decode_module(j);
    } else {
        c.group = std::make_unique<group_node>(decode_group(j.at("group").get<std::string>(), j));
    }
    return c;
}

inline group_node decode_group(std::string name, const nlohmann::json& j) {
    group_node g;
    g.name = std::move(name);
    for (const nlohmann::json& child : j.value("modules", nlohmann::json::array())) {
        g.modules.push_back(decode_child(child));
    }
    if (j.contains("expose")) {
        const nlohmann::json& expose = j.at("expose");
        const nlohmann::json inputs = expose.value("inputs", nlohmann::json::object());
        for (const auto& [alias, path] : inputs.items()) {
            g.expose_inputs.emplace_back(alias, path.get<std::string>());
        }
        const nlohmann::json outputs = expose.value("outputs", nlohmann::json::object());
        for (const auto& [alias, path] : outputs.items()) {
            g.expose_outputs.emplace_back(alias, path.get<std::string>());
        }
    }
    for (const nlohmann::json& c : j.value("connections", nlohmann::json::array())) {
        g.connections.push_back(
            {c.at("from").get<std::string>(), c.at("to").get<std::string>(), c.value("replay", false)});
    }
    return g;
}

inline nlohmann::json encode_group_body(const group_node& g);

inline nlohmann::json encode_child(const child_node& c) {
    if (c.module) {
        nlohmann::json j{{"module", c.module->factory}};
        if (c.module->name != c.module->factory) {
            j["name"] = c.module->name;
        }
        if (c.module->factory_version) {
            j["version"] = c.module->factory_version->to_string();
        }
        if (!c.module->properties.empty()) {
            nlohmann::json props = nlohmann::json::object();
            for (const auto& [name, value] : c.module->properties) {
                props[name] = value;
            }
            j["properties"] = std::move(props);
        }
        return j;
    }
    nlohmann::json j = encode_group_body(*c.group);
    j["group"] = c.group->name;
    return j;
}

inline nlohmann::json encode_group_body(const group_node& g) {
    nlohmann::json j = nlohmann::json::object();
    if (!g.modules.empty()) {
        nlohmann::json modules = nlohmann::json::array();
        for (const child_node& c : g.modules) {
            modules.push_back(encode_child(c));
        }
        j["modules"] = std::move(modules);
    }
    if (!g.expose_inputs.empty() || !g.expose_outputs.empty()) {
        nlohmann::json expose = nlohmann::json::object();
        if (!g.expose_inputs.empty()) {
            nlohmann::json inputs = nlohmann::json::object();
            for (const auto& [alias, path] : g.expose_inputs) {
                inputs[alias] = path;
            }
            expose["inputs"] = std::move(inputs);
        }
        if (!g.expose_outputs.empty()) {
            nlohmann::json outputs = nlohmann::json::object();
            for (const auto& [alias, path] : g.expose_outputs) {
                outputs[alias] = path;
            }
            expose["outputs"] = std::move(outputs);
        }
        j["expose"] = std::move(expose);
    }
    if (!g.connections.empty()) {
        nlohmann::json connections = nlohmann::json::array();
        for (const connection_node& c : g.connections) {
            nlohmann::json cj{{"from", c.from}, {"to", c.to}};
            if (c.replay) {
                cj["replay"] = true;
            }
            connections.push_back(std::move(cj));
        }
        j["connections"] = std::move(connections);
    }
    return j;
}

}  // namespace detail

/// Converts a JSON document into the typed model.
/// @param doc document that has already passed validate() — that is the contract
/// @throws std::logic_error if the document turns out not to match the schema after all
[[nodiscard]] inline config decode(const nlohmann::json& doc) {
    config cfg;
    const std::optional<version> schema = try_parse_version(doc.at("version").get<std::string>());
    if (!schema) {
        throw std::logic_error("decode after validate: bad schema version");
    }
    cfg.schema = *schema;
    for (const nlohmann::json& p : doc.value("plugins", nlohmann::json::array())) {
        cfg.plugins.push_back(p.get<std::string>());
    }
    cfg.pipeline = detail::decode_group("root", doc.at("pipeline"));
    for (const nlohmann::json& t : doc.value("threads", nlohmann::json::array())) {
        thread_node n;
        n.name = t.at("name").get<std::string>();
        const std::string mode = t.value("mode", "on_demand");
        if (mode == "throttled") {
            n.mode = thread_mode::throttled;
            n.period = std::chrono::milliseconds(t.at("period_ms").get<int>());
        } else if (mode == "spinning") {
            n.mode = thread_mode::spinning;
        }
        cfg.threads.push_back(std::move(n));
    }
    const nlohmann::json assign = doc.value("assign", nlohmann::json::object());
    for (const auto& [group_path, thread] : assign.items()) {
        cfg.assignments.emplace_back(group_path, thread.get<std::string>());
    }
    return cfg;
}

/// Converts the model back into JSON, the inverse of decode. Produces the canonical form: defaults
/// omitted, the thread mode spelled out, property values written as scalars of their own type. The
/// invariants are that validate(encode(cfg)) is empty and encode(decode(doc)) == doc for a
/// canonical document. The order of expose aliases does not survive a round trip, since a JSON
/// object sorts its keys — which does not affect the meaning of the config.
[[nodiscard]] inline nlohmann::json encode(const config& cfg) {
    nlohmann::json doc = nlohmann::json::object();
    doc["version"] = cfg.schema.to_string();
    if (!cfg.plugins.empty()) {
        doc["plugins"] = cfg.plugins;
    }
    doc["pipeline"] = detail::encode_group_body(cfg.pipeline);
    if (!cfg.threads.empty()) {
        nlohmann::json threads = nlohmann::json::array();
        for (const thread_node& t : cfg.threads) {
            nlohmann::json tj{{"name", t.name}};
            switch (t.mode) {
                case thread_mode::on_demand:
                    tj["mode"] = "on_demand";
                    break;
                case thread_mode::throttled:
                    tj["mode"] = "throttled";
                    tj["period_ms"] = static_cast<int>(t.period.count());
                    break;
                case thread_mode::spinning:
                    tj["mode"] = "spinning";
                    break;
            }
            threads.push_back(std::move(tj));
        }
        doc["threads"] = std::move(threads);
    }
    if (!cfg.assignments.empty()) {
        nlohmann::json assign = nlohmann::json::object();
        for (const auto& [path, thread] : cfg.assignments) {
            assign[path] = thread;
        }
        doc["assign"] = std::move(assign);
    }
    return doc;
}

}  // namespace atp::runtime

#endif
