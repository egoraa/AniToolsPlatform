// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_RUNTIME_CONFIG_VALIDATOR_HPP
#define ATP_RUNTIME_CONFIG_VALIDATOR_HPP

#include <cstddef>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <atp/config/node.hpp>
#include <atp/config/read.hpp>
#include <atp/runtime/config_model.hpp>
#include <atp/support/version.hpp>

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

    void check_keys(const atp::config::node& node,
                    const std::string& path,
                    std::initializer_list<const char*> allowed) {
        const std::unordered_set<std::string> allowed_set(allowed.begin(), allowed.end());
        for (const auto& entry : node.entries()) {
            if (!allowed_set.contains(entry.first)) {
                error(path, "unknown key '" + entry.first + "'");
            }
        }
    }

    bool check_name(const atp::config::node& node, const std::string& path) {
        if (!node.is_string()) {
            error(path, "must be a non-empty string without '.'");
            return false;
        }
        const std::string text = node.as_string();
        if (text.empty() || text.contains('.')) {
            error(path, "must be a non-empty string without '.'");
            return false;
        }
        return true;
    }

    void check_port_path(const atp::config::node& node, const std::string& path) {
        if (!node.is_string()) {
            error(path, "must be a string '<child>.<port>'");
            return;
        }
        const std::string text = node.as_string();
        const auto dot = text.find('.');
        if (dot == std::string::npos || dot == 0 || dot + 1 == text.size() ||
            text.find('.', dot + 1) != std::string::npos) {
            error(path, "expected '<child>.<port>', got '" + text + "'");
        }
    }

    /// Reads the document's "configs" block into config_names. The contents of an entry are not
    /// checked by anything — having no schema is the whole point of the block — but the entry itself
    /// has to be an object or a "file:" path, so a module's config root is an object whichever
    /// spelling reached it, and the name may not contain a colon, since that is what tells a
    /// reference from a source prefix.
    ///
    /// An entry may **not** be a bare reference to another entry, and that single rule is the whole
    /// defence against cycles: a chain of references is then exactly one step long, so no visited set
    /// and no depth counter is needed anywhere.
    ///
    /// A name whose value was rejected is still recorded, so that a module referring to it is not
    /// blamed for a dangling reference on top of the one real error — the same reasoning as
    /// configs_usable, one level down.
    void check_configs(const atp::config::node& doc) {
        const atp::config::node* configs = doc.find("configs");
        if (configs == nullptr) {
            return;
        }
        if (!configs->is_object()) {
            error("configs", "must be an object of name -> object");
            configs_usable = false;
            return;
        }
        for (const auto& [name, value] : configs->entries()) {
            if (name.contains(':')) {
                error("configs", "entry name '" + name + "' may not contain ':'");
                continue;
            }
            if (value.is_string()) {
                check_config_file("configs." + name, value.as_string());
            } else if (!value.is_object()) {
                error("configs." + name, "must be an object or a 'file:' path");
                continue;
            }
            config_names.insert(name);
        }
    }

    /// A module's "config" is the block itself, a string naming an entry of "configs", or a "file:"
    /// path.
    void check_config(const atp::config::node& node, const std::string& path) {
        if (node.is_object()) {
            return;
        }
        if (!node.is_string()) {
            error(path, "must be an object or a string naming an entry of 'configs'");
            return;
        }
        const std::string text = node.as_string();
        const std::optional<std::string> ref = parse_config_ref(text);
        if (!ref) {
            const std::string source = text.substr(0, text.find(':'));
            if (source == "file") {
                check_config_file(path, text);
                return;
            }
            error(path, "unknown config source '" + source + "'");
        } else if (configs_usable && !config_names.contains(*ref)) {
            error(path, "no entry named '" + *ref + "' in 'configs'");
        }
    }

    /// Shape of a "file:" string, which is all a validator can say about it: whether the file is there,
    /// parses, or holds an object is a question for the builder, the only place that knows the
    /// document's own directory to resolve a relative path against.
    void check_config_file(const std::string& path, const std::string& text) {
        if (!text.starts_with("file:")) {
            error(path, "must be an object or a 'file:' path");
            return;
        }
        if (text.size() == std::string_view("file:").size()) {
            error(path, "names no file after 'file:'");
        }
    }

    void check_version(const atp::config::node& doc) {
        const atp::config::node* found = doc.find("version");
        if (found == nullptr) {
            error("version", "required field is missing");
            return;
        }
        if (!found->is_string()) {
            error("version", "must be a string");
            return;
        }
        const std::optional<version> v = try_parse_version(found->as_string());
        if (!v) {
            error("version", "invalid format '" + found->as_string() + "'");
            return;
        }
        if (v->parts[0] != config_schema_version.parts[0] || v->parts[1] > config_schema_version.parts[1]) {
            error("version", "config schema " + v->to_string() + " is not supported (application supports " +
                                 config_schema_version.to_string() + ")");
        }
    }

    void check_expose_map(const atp::config::node& node, const std::string& path) {
        if (!node.is_object()) {
            error(path, "must be an object of alias -> '<child>.<port>'");
            return;
        }
        for (const auto& [alias, port] : node.entries()) {
            if (alias.empty() || alias.contains('.')) {
                error(path, "bad alias '" + alias + "'");
            }
            check_port_path(port, path + "." + alias);
        }
    }

    void check_group_body(const atp::config::node& node, const std::string& path) {
        if (const atp::config::node* modules = node.find("modules"); modules != nullptr) {
            if (!modules->is_array()) {
                error(path + ".modules", "must be an array");
            } else {
                std::unordered_set<std::string> names;
                for (std::size_t index = 0; index < modules->size(); ++index) {
                    check_child((*modules)[index], path + ".modules[" + std::to_string(index) + "]", names);
                }
            }
        }
        if (const atp::config::node* expose = node.find("expose"); expose != nullptr) {
            const std::string expose_path = path + ".expose";
            if (!expose->is_object()) {
                error(expose_path, "must be an object");
            } else {
                check_keys(*expose, expose_path, {"inputs", "outputs"});
                if (const atp::config::node* inputs = expose->find("inputs"); inputs != nullptr) {
                    check_expose_map(*inputs, expose_path + ".inputs");
                }
                if (const atp::config::node* outputs = expose->find("outputs"); outputs != nullptr) {
                    check_expose_map(*outputs, expose_path + ".outputs");
                }
            }
        }
        if (const atp::config::node* connections = node.find("connections"); connections != nullptr) {
            if (!connections->is_array()) {
                error(path + ".connections", "must be an array");
            } else {
                for (std::size_t index = 0; index < connections->size(); ++index) {
                    const atp::config::node& c = (*connections)[index];
                    const std::string cpath = path + ".connections[" + std::to_string(index) + "]";
                    if (!c.is_object()) {
                        error(cpath, "must be an object {from, to}");
                    } else {
                        check_keys(c, cpath, {"from", "to"});
                        if (const atp::config::node* from = c.find("from"); from != nullptr) {
                            check_port_path(*from, cpath + ".from");
                        } else {
                            error(cpath, "'from' is required");
                        }
                        if (const atp::config::node* to = c.find("to"); to != nullptr) {
                            check_port_path(*to, cpath + ".to");
                        } else {
                            error(cpath, "'to' is required");
                        }
                    }
                }
            }
        }
    }

    void check_child(const atp::config::node& node, const std::string& path, std::unordered_set<std::string>& names) {
        if (!node.is_object()) {
            error(path, "must be an object");
            return;
        }
        const atp::config::node* module = node.find("module");
        const atp::config::node* group = node.find("group");
        if ((module != nullptr) == (group != nullptr)) {
            error(path, "exactly one of 'module' or 'group' is required");
            return;
        }
        std::string child_name;
        if (module != nullptr) {
            check_keys(node, path, {"module", "name", "version", "properties", "config"});
            if (check_name(*module, path + ".module")) {
                child_name = module->as_string();
            }
            if (const atp::config::node* name = node.find("name");
                name != nullptr && check_name(*name, path + ".name")) {
                child_name = name->as_string();
            }
            if (const atp::config::node* declared = node.find("version"); declared != nullptr) {
                if (!declared->is_string() || !try_parse_version(declared->as_string())) {
                    error(path + ".version", "invalid version");
                }
            }
            if (const atp::config::node* props = node.find("properties"); props != nullptr) {
                const std::string ppath = path + ".properties";
                if (!props->is_object()) {
                    error(ppath, "must be an object of name -> scalar");
                } else {
                    for (const auto& [pname, pvalue] : props->entries()) {
                        if (!pvalue.is_int() && !pvalue.is_double() && !pvalue.is_string() && !pvalue.is_bool()) {
                            error(ppath + "." + pname, "must be a scalar (number, string or boolean)");
                        }
                    }
                }
            }
            if (const atp::config::node* cfg = node.find("config"); cfg != nullptr) {
                check_config(*cfg, path + ".config");
            }
        } else {
            check_keys(node, path, {"group", "modules", "expose", "connections"});
            if (check_name(*group, path + ".group")) {
                child_name = group->as_string();
            }
            check_group_body(node, path);
        }
        if (!child_name.empty() && !names.insert(child_name).second) {
            error(path, "duplicate child name '" + child_name + "'");
        }
    }

    bool group_path_exists(const atp::config::node& pipeline, const std::string& path) const {
        const atp::config::node* current = &pipeline;
        std::size_t begin = 0;
        while (begin <= path.size()) {
            const std::size_t dot = path.find('.', begin);
            const std::string segment = path.substr(begin, dot == std::string::npos ? dot : dot - begin);
            const atp::config::node* next = nullptr;
            if (const atp::config::node* modules = current->find("modules");
                modules != nullptr && modules->is_array()) {
                for (std::size_t i = 0; i < modules->size(); ++i) {
                    const atp::config::node& child = (*modules)[i];
                    const atp::config::node* group = child.is_object() ? child.find("group") : nullptr;
                    if (group != nullptr && group->is_string() && group->as_string() == segment) {
                        next = &child;
                        break;
                    }
                }
            }
            if (next == nullptr) {
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
[[nodiscard]] inline std::vector<std::string> validate(const atp::config::node& doc) {
    detail::validator v;
    if (!doc.is_object()) {
        v.error("$", "config root must be an object");
        return v.errors;
    }
    v.check_version(doc);
    v.check_keys(doc, "$", {"version", "plugins", "pipeline", "threads", "assign", "configs"});
    v.check_configs(doc);

    if (const atp::config::node* plugins = doc.find("plugins"); plugins != nullptr) {
        if (!plugins->is_array()) {
            v.error("plugins", "must be an array of file paths");
        } else {
            for (std::size_t index = 0; index < plugins->size(); ++index) {
                const atp::config::node& p = (*plugins)[index];
                if (!p.is_string() || p.as_string().empty()) {
                    v.error("plugins[" + std::to_string(index) + "]", "must be a non-empty string");
                }
            }
        }
    }

    const atp::config::node* pipeline = doc.find("pipeline");
    if (pipeline == nullptr || !pipeline->is_object()) {
        v.error("pipeline", "required object is missing");
        return v.errors;
    }
    v.check_keys(*pipeline, "pipeline", {"modules", "expose", "connections"});
    v.check_group_body(*pipeline, "pipeline");

    std::unordered_set<std::string> thread_names;
    if (const atp::config::node* threads = doc.find("threads"); threads != nullptr) {
        if (!threads->is_array()) {
            v.error("threads", "must be an array");
        } else {
            for (std::size_t index = 0; index < threads->size(); ++index) {
                const atp::config::node& t = (*threads)[index];
                const std::string tpath = "threads[" + std::to_string(index) + "]";
                if (!t.is_object()) {
                    v.error(tpath, "must be an object {name, mode[, period_ms]}");
                    continue;
                }
                v.check_keys(t, tpath, {"name", "mode", "period_ms"});
                const atp::config::node* name = t.find("name");
                if (name == nullptr || !v.check_name(*name, tpath + ".name")) {
                    continue;
                }
                if (!thread_names.insert(name->as_string()).second) {
                    v.error(tpath, "duplicate thread name '" + name->as_string() + "'");
                }
                const std::string mode = atp::config::string_or(t.find("mode"), "on_demand");
                if (mode != "on_demand" && mode != "throttled" && mode != "spinning") {
                    v.error(tpath + ".mode", "unknown mode '" + mode + "'");
                }
                const atp::config::node* period = t.find("period_ms");
                if (mode == "throttled") {
                    if (period == nullptr || !period->is_int() || period->as_int() <= 0) {
                        v.error(tpath, "throttled thread requires a positive integer 'period_ms'");
                    }
                } else if (period != nullptr) {
                    v.error(tpath, "'period_ms' is only for throttled mode");
                }
            }
        }
    }

    if (const atp::config::node* assign = doc.find("assign"); assign != nullptr) {
        if (!assign->is_object()) {
            v.error("assign", "must be an object of group path -> thread name");
        } else {
            for (const auto& [group_path, thread] : assign->entries()) {
                const std::string apath = "assign." + group_path;
                if (!thread.is_string() || !thread_names.contains(thread.as_string())) {
                    v.error(apath, "unknown thread");
                }
                if (!v.group_path_exists(*pipeline, group_path)) {
                    v.error(apath, "group path does not exist in pipeline");
                }
            }
        }
    }

    return v.errors;
}

}  // namespace atp::runtime

#endif
