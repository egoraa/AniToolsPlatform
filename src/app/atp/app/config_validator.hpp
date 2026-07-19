#ifndef ATP_APP_CONFIG_VALIDATOR_HPP
#define ATP_APP_CONFIG_VALIDATOR_HPP

#include <cstddef>
#include <initializer_list>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include <atp/app/config_model.hpp>
#include <atp/version.hpp>

namespace atp::app {

namespace detail {

// Копилка ошибок: правила пишут в неё и продолжают — пользователь получает
// все проблемы конфига за один запуск, а не по одной за итерацию.
class validator {
   public:
    std::vector<std::string> errors;

    void error(const std::string& path, const std::string& message) {
        errors.push_back(path + ": " + message);
    }

    // Неизвестный ключ — почти всегда опечатка; молчание здесь стоило бы
    // пользователю молча пропавшей настройки.
    void check_keys(const nlohmann::json& node, const std::string& path, std::initializer_list<const char*> allowed) {
        const std::unordered_set<std::string> allowed_set(allowed.begin(), allowed.end());
        for (const auto& [key, value] : node.items()) {
            if (!allowed_set.contains(key)) {
                error(path, "unknown key '" + key + "'");
            }
        }
    }

    // Имя (ребёнка, потока, алиаса): непустая строка без точки — точка
    // зарезервирована разделителем путей.
    bool check_name(const nlohmann::json& node, const std::string& path) {
        if (!node.is_string() || node.get<std::string>().empty() ||
            node.get<std::string>().find('.') != std::string::npos) {
            error(path, "must be a non-empty string without '.'");
            return false;
        }
        return true;
    }

    // Путь порта "дитя.порт" — ровно одна точка, обе половины непусты
    // (зеркало group::split_path).
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
        // Мажор — совместимость, минор — «не новее нас»: неизвестные поля
        // будущих миноров нельзя молча игнорировать.
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
            if (alias.empty() || alias.find('.') != std::string::npos) {
                error(path, "bad alias '" + alias + "'");
            }
            check_port_path(port_path, path + "." + alias);
        }
    }

    void check_group_body(const nlohmann::json& node, const std::string& path) {
        if (node.contains("children")) {
            if (!node.at("children").is_array()) {
                error(path + ".children", "must be an array");
            } else {
                std::unordered_set<std::string> names;
                std::size_t index = 0;
                for (const nlohmann::json& child : node.at("children")) {
                    check_child(child, path + ".children[" + std::to_string(index) + "]", names);
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
                        error(cpath, "must be an object {from, to[, replay]}");
                    } else {
                        check_keys(c, cpath, {"from", "to", "replay"});
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
                        if (c.contains("replay") && !c.at("replay").is_boolean()) {
                            error(cpath + ".replay", "must be a boolean");
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
            check_keys(node, path, {"module", "name", "version", "params"});
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
            // params — любой JSON-узел: интерпретация за фабрикой модуля
        } else {
            check_keys(node, path, {"group", "children", "expose", "connections"});
            if (check_name(node.at("group"), path + ".group")) {
                child_name = node.at("group").get<std::string>();
            }
            check_group_body(node, path);
        }
        if (!child_name.empty() && !names.insert(child_name).second) {
            error(path, "duplicate child name '" + child_name + "'");
        }
    }

    // Путь assign проверяется по самому дереву: сегменты спускаются по
    // именам подгрупп развёрнутого документа.
    bool group_path_exists(const nlohmann::json& pipeline, const std::string& path) const {
        const nlohmann::json* current = &pipeline;
        std::size_t begin = 0;
        while (begin <= path.size()) {
            const std::size_t dot = path.find('.', begin);
            const std::string segment = path.substr(begin, dot == std::string::npos ? dot : dot - begin);
            const nlohmann::json* next = nullptr;
            if (current->contains("children") && current->at("children").is_array()) {
                for (const nlohmann::json& child : current->at("children")) {
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

// Проверка развёрнутого документа (после load_config, до decode). Пустой
// вектор — конфиг валиден; иначе каждая запись — «json-путь: сообщение».
// Правила платформы, требующие реестра (существование модулей, типы
// портов, межпоточность), проверяются позже — при сборке.
[[nodiscard]] inline std::vector<std::string> validate(const nlohmann::json& doc) {
    detail::validator v;
    if (!doc.is_object()) {
        v.error("$", "config root must be an object");
        return v.errors;
    }
    v.check_version(doc);
    v.check_keys(doc, "$", {"version", "plugins", "pipeline", "threads", "assign"});

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
        return v.errors;  // без дерева дальнейшие проверки бессмысленны
    }
    v.check_keys(doc.at("pipeline"), "pipeline", {"children", "expose", "connections"});
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
                    // те же контракты, что у pipeline_runner::add_thread —
                    // но с путём конфига вместо исключения на пол-дороге
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

}  // namespace atp::app

#endif  // ATP_APP_CONFIG_VALIDATOR_HPP
