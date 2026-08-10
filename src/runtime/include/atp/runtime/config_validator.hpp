// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_RUNTIME_CONFIG_VALIDATOR_HPP
#define ATP_RUNTIME_CONFIG_VALIDATOR_HPP

#include <cstddef>
#include <initializer_list>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include <atp/runtime/config_model.hpp>
#include <atp/version.hpp>

namespace atp::runtime {

namespace detail {

class validator {
   public:
    std::vector<std::string> errors;

    /// Entry names of the document's "configs" block, collected before the pipeline is walked because
    /// check_child cannot see the document root from where it stands.
    std::unordered_set<std::string> config_names;

    /// Whether the "configs" block itself parsed. A malformed block leaves config_names empty, and
    /// checking references against it anyway would blame every reference in the document for a dangling
    /// name on top of the one real error — the cause has to be named once.
    bool configs_usable = true;

    void error(const std::string& path, const std::string& message) {
        errors.push_back(path + ": " + message);
    }

    void check_keys(const nlohmann::json& node, const std::string& path, std::initializer_list<const char*> allowed) {
        const std::unordered_set<std::string> allowed_set(allowed.begin(), allowed.end());
        for (const auto& [key, value] : node.items()) {
            if (!allowed_set.contains(key)) {
                error(path, "unknown key '" + key + "'");
            }
        }
    }

    bool check_name(const nlohmann::json& node, const std::string& path) {
        if (!node.is_string() || node.get<std::string>().empty() || node.get<std::string>().contains('.')) {
            error(path, "must be a non-empty string without '.'");
            return false;
        }
        return true;
    }

    void check_port_path(const nlohmann::json& node, const std::string& path) {
        if (!node.is_string()) {
            error(path, "must be a string '<child>.<port>'");
            return;
        }
        const std::string text = node.get<std::string>();
        const auto dot = text.find('.');
        if (dot == std::string::npos || dot == 0 || dot + 1 == text.size() ||
            text.find('.', dot + 1) != std::string::npos) {
            error(path, "expected '<child>.<port>', got '" + text + "'");
        }
    }

    /// Reads the document's "configs" block into config_names. The contents of an entry are not
    /// checked by anything — having no schema is the whole point of the block — but the entry itself
    /// has to be an object, so a module's config root is an object whichever spelling reached it, and
    /// the name may not contain a colon, since that is what tells a reference from a source prefix.
    void check_configs(const nlohmann::json& doc) {
        if (!doc.contains("configs")) {
            return;
        }
        const nlohmann::json& configs = doc.at("configs");
        if (!configs.is_object()) {
            error("configs", "must be an object of name -> object");
            configs_usable = false;
            return;
        }
        for (const auto& [name, value] : configs.items()) {
            if (name.contains(':')) {
                error("configs", "entry name '" + name + "' may not contain ':'");
                continue;
            }
            if (!value.is_object()) {
                error("configs." + name, "must be an object");
                continue;
            }
            config_names.insert(name);
        }
    }

    /// A module's "config" is either the block itself or a string naming an entry of "configs".
    void check_config(const nlohmann::json& node, const std::string& path) {
        if (node.is_object()) {
            return;
        }
        if (!node.is_string()) {
            error(path, "must be an object or a string naming an entry of 'configs'");
            return;
        }
        const std::string text = node.get<std::string>();
        const std::optional<std::string> ref = parse_config_ref(text);
        if (!ref) {
            error(path, "unknown config source '" + text.substr(0, text.find(':')) + "'");
        } else if (configs_usable && !config_names.contains(*ref)) {
            error(path, "no entry named '" + *ref + "' in 'configs'");
        }
    }

    void check_version(const nlohmann::json& doc) {
        if (!doc.contains("version")) {
            error("version", "required field is missing");
            return;
        }
        if (!doc.at("version").is_string()) {
            error("version", "must be a string");
            return;
        }
        const std::optional<version> v = try_parse_version(doc.at("version").get<std::string>());
        if (!v) {
            error("version", "invalid format '" + doc.at("version").get<std::string>() + "'");
            return;
        }
        if (v->parts[0] != config_schema_version.parts[0] || v->parts[1] > config_schema_version.parts[1]) {
            error("version", "config schema " + v->to_string() + " is not supported (application supports " +
                                 config_schema_version.to_string() + ")");
        }
    }

    void check_expose_map(const nlohmann::json& node, const std::string& path) {
        if (!node.is_object()) {
            error(path, "must be an object of alias -> '<child>.<port>'");
            return;
        }
        for (const auto& [alias, port_path] : node.items()) {
            if (alias.empty() || alias.contains('.')) {
                error(path, "bad alias '" + alias + "'");
            }
            check_port_path(port_path, path + "." + alias);
        }
    }

    void check_group_body(const nlohmann::json& node, const std::string& path) {
        if (node.contains("modules")) {
            if (!node.at("modules").is_array()) {
                error(path + ".modules", "must be an array");
            } else {
                std::unordered_set<std::string> names;
                std::size_t index = 0;
                for (const nlohmann::json& child : node.at("modules")) {
                    check_child(child, path + ".modules[" + std::to_string(index) + "]", names);
                    ++index;
                }
            }
        }
        if (node.contains("expose")) {
            const std::string expose_path = path + ".expose";
            if (!node.at("expose").is_object()) {
                error(expose_path, "must be an object");
            } else {
                check_keys(node.at("expose"), expose_path, {"inputs", "outputs"});
                if (node.at("expose").contains("inputs")) {
                    check_expose_map(node.at("expose").at("inputs"), expose_path + ".inputs");
                }
                if (node.at("expose").contains("outputs")) {
                    check_expose_map(node.at("expose").at("outputs"), expose_path + ".outputs");
                }
            }
        }
        if (node.contains("connections")) {
            if (!node.at("connections").is_array()) {
                error(path + ".connections", "must be an array");
            } else {
                std::size_t index = 0;
                for (const nlohmann::json& c : node.at("connections")) {
                    const std::string cpath = path + ".connections[" + std::to_string(index) + "]";
                    if (!c.is_object()) {
                        error(cpath, "must be an object {from, to}");
                    } else {
                        check_keys(c, cpath, {"from", "to"});
                        if (c.contains("from")) {
                            check_port_path(c.at("from"), cpath + ".from");
                        } else {
                            error(cpath, "'from' is required");
                        }
                        if (c.contains("to")) {
                            check_port_path(c.at("to"), cpath + ".to");
                        } else {
                            error(cpath, "'to' is required");
                        }
                    }
                    ++index;
                }
            }
        }
    }

    void check_child(const nlohmann::json& node, const std::string& path, std::unordered_set<std::string>& names) {
        if (!node.is_object()) {
            error(path, "must be an object");
            return;
        }
        const bool is_module = node.contains("module");
        const bool is_group = node.contains("group");
        if (is_module == is_group) {
            error(path, "exactly one of 'module' or 'group' is required");
            return;
        }
        std::string child_name;
        if (is_module) {
            check_keys(node, path, {"module", "name", "version", "properties", "config"});
            if (check_name(node.at("module"), path + ".module")) {
                child_name = node.at("module").get<std::string>();
            }
            if (node.contains("name") && check_name(node.at("name"), path + ".name")) {
                child_name = node.at("name").get<std::string>();
            }
            if (node.contains("version")) {
                if (!node.at("version").is_string() || !try_parse_version(node.at("version").get<std::string>())) {
                    error(path + ".version", "invalid version");
                }
            }
            if (node.contains("properties")) {
                const std::string ppath = path + ".properties";
                if (!node.at("properties").is_object()) {
                    error(ppath, "must be an object of name -> scalar");
                } else {
                    const nlohmann::json& props = node.at("properties");
                    for (const auto& [pname, pvalue] : props.items()) {
                        if (!pvalue.is_number() && !pvalue.is_string() && !pvalue.is_boolean()) {
                            error(ppath + "." + pname, "must be a scalar (number, string or boolean)");
                        }
                    }
                }
            }
            if (node.contains("config")) {
                check_config(node.at("config"), path + ".config");
            }
        } else {
            check_keys(node, path, {"group", "modules", "expose", "connections"});
            if (check_name(node.at("group"), path + ".group")) {
                child_name = node.at("group").get<std::string>();
            }
            check_group_body(node, path);
        }
        if (!child_name.empty() && !names.insert(child_name).second) {
            error(path, "duplicate child name '" + child_name + "'");
        }
    }

    bool group_path_exists(const nlohmann::json& pipeline, const std::string& path) const {
        const nlohmann::json* current = &pipeline;
        std::size_t begin = 0;
        while (begin <= path.size()) {
            const std::size_t dot = path.find('.', begin);
            const std::string segment = path.substr(begin, dot == std::string::npos ? dot : dot - begin);
            const nlohmann::json* next = nullptr;
            if (current->contains("modules") && current->at("modules").is_array()) {
                for (const nlohmann::json& child : current->at("modules")) {
                    if (child.is_object() && child.contains("group") && child.at("group").is_string() &&
                        child.at("group").get<std::string>() == segment) {
                        next = &child;
                        break;
                    }
                }
            }
            if (!next) {
                return false;
            }
            current = next;
            if (dot == std::string::npos) {
                return true;
            }
            begin = dot + 1;
        }
        return false;
    }
};

}  // namespace detail

/// Validates an expanded document, after load_config and before decode. Platform rules that need
/// the registry (whether the modules exist, port types, cross-thread safety) are checked later, at
/// build time.
/// @return an empty vector if the config is valid; otherwise one "json path: message" entry per
///         problem
[[nodiscard]] inline std::vector<std::string> validate(const nlohmann::json& doc) {
    detail::validator v;
    if (!doc.is_object()) {
        v.error("$", "config root must be an object");
        return v.errors;
    }
    v.check_version(doc);
    v.check_keys(doc, "$", {"version", "plugins", "pipeline", "threads", "assign", "configs"});
    v.check_configs(doc);

    if (doc.contains("plugins")) {
        if (!doc.at("plugins").is_array()) {
            v.error("plugins", "must be an array of file paths");
        } else {
            std::size_t index = 0;
            for (const nlohmann::json& p : doc.at("plugins")) {
                if (!p.is_string() || p.get<std::string>().empty()) {
                    v.error("plugins[" + std::to_string(index) + "]", "must be a non-empty string");
                }
                ++index;
            }
        }
    }

    if (!doc.contains("pipeline") || !doc.at("pipeline").is_object()) {
        v.error("pipeline", "required object is missing");
        return v.errors;
    }
    v.check_keys(doc.at("pipeline"), "pipeline", {"modules", "expose", "connections"});
    v.check_group_body(doc.at("pipeline"), "pipeline");

    std::unordered_set<std::string> thread_names;
    if (doc.contains("threads")) {
        if (!doc.at("threads").is_array()) {
            v.error("threads", "must be an array");
        } else {
            std::size_t index = 0;
            for (const nlohmann::json& t : doc.at("threads")) {
                const std::string tpath = "threads[" + std::to_string(index) + "]";
                if (!t.is_object()) {
                    v.error(tpath, "must be an object {name, mode[, period_ms]}");
                    ++index;
                    continue;
                }
                v.check_keys(t, tpath, {"name", "mode", "period_ms"});
                if (!t.contains("name") || !v.check_name(t.at("name"), tpath + ".name")) {
                    ++index;
                    continue;
                }
                if (!thread_names.insert(t.at("name").get<std::string>()).second) {
                    v.error(tpath, "duplicate thread name '" + t.at("name").get<std::string>() + "'");
                }
                const std::string mode = t.value("mode", "on_demand");
                if (mode != "on_demand" && mode != "throttled" && mode != "spinning") {
                    v.error(tpath + ".mode", "unknown mode '" + mode + "'");
                }
                const bool has_period = t.contains("period_ms");
                if (mode == "throttled") {
                    if (!has_period || !t.at("period_ms").is_number_integer() || t.at("period_ms").get<int>() <= 0) {
                        v.error(tpath, "throttled thread requires a positive integer 'period_ms'");
                    }
                } else if (has_period) {
                    v.error(tpath, "'period_ms' is only for throttled mode");
                }
                ++index;
            }
        }
    }

    if (doc.contains("assign")) {
        if (!doc.at("assign").is_object()) {
            v.error("assign", "must be an object of group path -> thread name");
        } else {
            for (const auto& [group_path, thread] : doc.at("assign").items()) {
                const std::string apath = "assign." + group_path;
                if (!thread.is_string() || !thread_names.contains(thread.get<std::string>())) {
                    v.error(apath, "unknown thread");
                }
                if (!v.group_path_exists(doc.at("pipeline"), group_path)) {
                    v.error(apath, "group path does not exist in pipeline");
                }
            }
        }
    }

    return v.errors;
}

}  // namespace atp::runtime

#endif
