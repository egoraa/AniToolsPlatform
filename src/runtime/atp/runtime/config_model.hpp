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

// Версия схемы конфига, которую понимает приложение: мажор конфига обязан
// совпадать, минор — не превышать наш (поля «из будущего» отклоняются,
// а не игнорируются молча). Само поле "version" — первое, что проверяется.
inline constexpr version config_schema_version{1, 0};

// Ошибка уровня приложения: чтение, инклуды, сборка по конфигу.
class config_error : public std::runtime_error {
   public:
    using std::runtime_error::runtime_error;
};

struct module_node {
    std::string factory;                     // имя фабрики в реестре
    std::string name;                        // имя ребёнка в группе (дефолт — имя фабрики)
    std::optional<version> factory_version;  // нет — последняя зарегистрированная
    std::string params;                      // сырой JSON узла params; "" — параметров нет
};

struct group_node;

// Ребёнок группы: заполнено ровно одно из полей (инвариант decode).
// unique_ptr — рекурсия типа; порядок в векторе значим — это порядок
// вставки в группу, то есть порядок каскадов жизненного цикла.
struct child_node {
    std::optional<module_node> module;
    std::unique_ptr<group_node> group;
};

struct connection_node {
    std::string from;  // пути "дитя.порт" в области видимости группы
    std::string to;
    bool replay = false;
};

struct group_node {
    std::string name;
    std::vector<child_node> children;
    std::vector<std::pair<std::string, std::string>> expose_inputs;  // алиас → "дитя.порт"
    std::vector<std::pair<std::string, std::string>> expose_outputs;
    std::vector<connection_node> connections;
};

struct thread_node {
    std::string name;
    thread_mode mode = thread_mode::on_demand;
    std::chrono::milliseconds period{};  // только для throttled
};

struct config {
    version schema;                    // поле "version" корневого документа
    std::vector<std::string> plugins;  // пути относительно каталога конфига
    group_node pipeline;               // корень; имя всегда "root"
    std::vector<thread_node> threads;
    std::vector<std::pair<std::string, std::string>> assignments;  // путь группы → имя потока
};

namespace detail {

// Decode доверяет форме: он зовётся после validate, поэтому расхождение —
// логическая ошибка программы, а не пользовательского конфига.

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
    if (j.contains("params")) {
        m.params = j.at("params").dump();  // компактно: фабрике важен смысл, не форматирование
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
    for (const nlohmann::json& child : j.value("children", nlohmann::json::array())) {
        g.children.push_back(decode_child(child));
    }
    if (j.contains("expose")) {
        const nlohmann::json& expose = j.at("expose");
        // value() отдаёт json по значению, а items() держит ссылку на него:
        // временный объект в заголовке range-for умирает до тела цикла, и
        // итерация читает освобождённую память. Держим результат в переменной.
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

// Encode пишет каноничную форму: дефолты опускаются — файл после studio
// не обрастает шумом, диффы конфигов остаются читаемыми.

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
        if (!c.module->params.empty()) {
            j["params"] = nlohmann::json::parse(c.module->params);  // params — JSON-узел, не строка
        }
        return j;
    }
    nlohmann::json j = encode_group_body(*c.group);
    j["group"] = c.group->name;
    return j;
}

inline nlohmann::json encode_group_body(const group_node& g) {
    nlohmann::json j = nlohmann::json::object();
    if (!g.children.empty()) {
        nlohmann::json children = nlohmann::json::array();
        for (const child_node& c : g.children) {
            children.push_back(encode_child(c));
        }
        j["children"] = std::move(children);
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

// JSON → типизированная модель. Контракт: документ уже прошёл validate.
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
    const nlohmann::json assign = doc.value("assign", nlohmann::json::object());  // см. про items() выше
    for (const auto& [group_path, thread] : assign.items()) {
        cfg.assignments.emplace_back(group_path, thread.get<std::string>());
    }
    return cfg;
}

// Модель → JSON, обратный к decode. Каноничная форма (дефолты опущены,
// mode потока явный, params — узлом); инварианты: validate(encode(cfg))
// пуст, encode(decode(doc)) == doc для каноничного документа. Порядок
// алиасов expose при round-trip не сохраняется — JSON-объект сортирует
// ключи; на смысл конфига это не влияет.
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

#endif  // ATP_RUNTIME_CONFIG_MODEL_HPP
