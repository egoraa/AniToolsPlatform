#ifndef ATP_STUDIO_DOCUMENT_HPP
#define ATP_STUDIO_DOCUMENT_HPP

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <atp/runtime/config_loader.hpp>
#include <atp/runtime/config_model.hpp>
#include <atp/runtime/config_validator.hpp>

namespace atp::studio {

// Позиция узла на канвасе — editor-метаданные, в конфиг пайплайна не
// попадают (sidecar-файл), float — родная система координат ImGui.
struct node_position {
    float x = 0.0f;
    float y = 0.0f;

    friend bool operator==(const node_position&, const node_position&) = default;
};

namespace detail {

// Спуск по пути групп ("" — корень, дальше сегменты по точкам). Модуль на
// пути или неизвестный сегмент — nullptr: вызывающий сам решает, ошибка это
// или проверка существования.
inline const runtime::group_node* find_group(const runtime::group_node& root, const std::string& path) {
    const runtime::group_node* current = &root;
    std::size_t begin = 0;
    while (!path.empty()) {
        const std::size_t dot = path.find('.', begin);
        const std::string segment = path.substr(begin, dot == std::string::npos ? dot : dot - begin);
        const runtime::group_node* next = nullptr;
        for (const runtime::child_node& c : current->children) {
            if (c.group && c.group->name == segment) {
                next = c.group.get();
                break;
            }
        }
        if (next == nullptr) {
            return nullptr;
        }
        current = next;
        if (dot == std::string::npos) {
            break;
        }
        begin = dot + 1;
    }
    return current;
}

inline runtime::group_node* find_group(runtime::group_node& root, const std::string& path) {
    // const-версия — единственная реализация; снятие const законно:
    // исходный объект неконстантен.
    return const_cast<runtime::group_node*>(find_group(std::as_const(root), path));
}

// Имя ребёнка/потока/алиаса: непустое, без точки (разделитель путей).
inline void check_name(const std::string& name, const char* what) {
    if (name.empty() || name.find('.') != std::string::npos) {
        throw runtime::config_error(std::string(what) + " '" + name + "' must be non-empty and contain no '.'");
    }
}

// Путь порта "дитя.порт" — ровно одна точка, обе половины непусты.
inline std::string port_path_child(const std::string& path) {
    const std::size_t dot = path.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 == path.size() ||
        path.find('.', dot + 1) != std::string::npos) {
        throw runtime::config_error("expected '<child>.<port>', got '" + path + "'");
    }
    return path.substr(0, dot);
}

inline const std::string& child_name(const runtime::child_node& c) {
    return c.module ? c.module->name : c.group->name;
}

inline runtime::child_node* find_child(runtime::group_node& g, const std::string& name) {
    for (runtime::child_node& c : g.children) {
        if (child_name(c) == name) {
            return &c;
        }
    }
    return nullptr;
}

// Переписать префикс "old." в пути порта (rename дитя).
inline void rewrite_port_prefix(std::string& port_path, const std::string& old_name, const std::string& new_name) {
    if (port_path.starts_with(old_name + ".")) {
        port_path = new_name + port_path.substr(old_name.size());
    }
}

// Полный путь узла: "группа.дитя", в корне — просто имя. Живёт здесь, а не в
// Qt-слое, потому что позициями узлов оперирует и не-Qt код (add_module).
[[nodiscard]] inline std::string full_path(const std::string& group_path, const std::string& child) {
    return group_path.empty() ? child : group_path + "." + child;
}

}  // namespace detail

// Редактируемый документ: типизированная модель конфига + editor-метаданные.
// Все операции редактирования проверяют инварианты и пишут снапшот в undo;
// позиции — визуальный слой вне undo.
class document {
   public:
    [[nodiscard]] static document create() {
        document d;
        d.cfg_.schema = runtime::config_schema_version;
        return d;
    }

    // Ошибки валидации агрегируются в один config_error — вызывающему не
    // нужен отдельный канал для списка.
    [[nodiscard]] static document open(const std::filesystem::path& file) {
        document d;
        {
            // Инклуды при сохранении расплющиваются — честно предупредить.
            // Ищем маркер в сыром тексте: после разворота границ файлов нет.
            std::ifstream in(file);
            std::stringstream raw;
            raw << in.rdbuf();
            d.had_includes_ = raw.str().find("\"$include\"") != std::string::npos;
        }
        const nlohmann::json doc = runtime::load_config(file);
        const std::vector<std::string> errors = runtime::validate(doc);
        if (!errors.empty()) {
            std::string message = "invalid config '" + file.string() + "':";
            for (const std::string& e : errors) {
                message += "\n  " + e;
            }
            throw runtime::config_error(message);
        }
        d.cfg_ = runtime::decode(doc);
        d.load_layout(layout_path(file));
        return d;
    }

    void save(const std::filesystem::path& file) const {
        std::ofstream out(file);
        if (!out) {
            throw runtime::config_error("cannot write config '" + file.string() + "'");
        }
        out << runtime::encode(cfg_).dump(4) << '\n';
        save_layout(layout_path(file));
    }

    [[nodiscard]] const runtime::config& config() const {
        return cfg_;
    }

    [[nodiscard]] bool had_includes() const {
        return had_includes_;
    }

    [[nodiscard]] const runtime::group_node* group_at(const std::string& path) const {
        return detail::find_group(cfg_.pipeline, path);
    }

    void add_module(const std::string& group_path,
                    const std::string& factory,
                    std::string name = {},
                    std::optional<version> factory_version = {},
                    std::string params = {}) {
        runtime::group_node& g = require_group(group_path);
        if (name.empty()) {
            name = factory;  // умолчание то же, что у decode
        }
        detail::check_name(name, "module name");
        require_free_name(g, name);
        if (!params.empty()) {
            params = parse_params(params);
        }
        snapshot();
        runtime::child_node c;
        c.module = runtime::module_node{factory, std::move(name), factory_version, std::move(params)};
        g.children.push_back(std::move(c));
    }

    void add_group(const std::string& group_path, const std::string& name) {
        runtime::group_node& g = require_group(group_path);
        detail::check_name(name, "group name");
        require_free_name(g, name);
        snapshot();
        runtime::child_node c;
        c.group = std::make_unique<runtime::group_node>();
        c.group->name = name;
        g.children.push_back(std::move(c));
    }

    void remove_child(const std::string& group_path, const std::string& name) {
        runtime::group_node& g = require_group(group_path);
        if (detail::find_child(g, name) == nullptr) {
            throw runtime::config_error("no child '" + name + "' in group '" + group_path + "'");
        }
        snapshot();
        const std::string prefix = name + ".";
        std::erase_if(g.connections, [&](const runtime::connection_node& c) {
            return c.from.starts_with(prefix) || c.to.starts_with(prefix);
        });
        std::erase_if(g.expose_inputs, [&](const auto& e) { return e.second.starts_with(prefix); });
        std::erase_if(g.expose_outputs, [&](const auto& e) { return e.second.starts_with(prefix); });
        const std::string full = join_path(group_path, name);
        std::erase_if(cfg_.assignments,
                      [&](const auto& a) { return a.first == full || a.first.starts_with(full + "."); });
        std::erase_if(g.children, [&](const runtime::child_node& c) { return detail::child_name(c) == name; });
        // позиции — визуальный слой, но осиротевшие ключи копить незачем
        std::erase_if(positions_, [&](const auto& p) { return p.first == full || p.first.starts_with(full + "."); });
    }

    void rename_child(const std::string& group_path, const std::string& old_name, const std::string& new_name) {
        runtime::group_node& g = require_group(group_path);
        runtime::child_node* child = detail::find_child(g, old_name);
        if (child == nullptr) {
            throw runtime::config_error("no child '" + old_name + "' in group '" + group_path + "'");
        }
        detail::check_name(new_name, "child name");
        if (new_name != old_name) {
            require_free_name(g, new_name);
        }
        snapshot();
        if (child->module) {
            child->module->name = new_name;
        } else {
            child->group->name = new_name;
        }
        for (runtime::connection_node& c : g.connections) {
            detail::rewrite_port_prefix(c.from, old_name, new_name);
            detail::rewrite_port_prefix(c.to, old_name, new_name);
        }
        for (auto& [alias, path] : g.expose_inputs) {
            detail::rewrite_port_prefix(path, old_name, new_name);
        }
        for (auto& [alias, path] : g.expose_outputs) {
            detail::rewrite_port_prefix(path, old_name, new_name);
        }
        const std::string old_full = join_path(group_path, old_name);
        const std::string new_full = join_path(group_path, new_name);
        for (auto& [path, thread] : cfg_.assignments) {
            rewrite_full_path(path, old_full, new_full);
        }
        std::map<std::string, node_position> renamed;
        for (auto& [path, p] : positions_) {
            std::string key = path;
            rewrite_full_path(key, old_full, new_full);
            renamed[key] = p;
        }
        positions_ = std::move(renamed);
    }

    void connect(const std::string& group_path, const std::string& from, const std::string& to, bool replay = false) {
        runtime::group_node& g = require_group(group_path);
        require_port_child(g, group_path, from);
        require_port_child(g, group_path, to);
        for (const runtime::connection_node& c : g.connections) {
            if (c.from == from && c.to == to) {
                throw runtime::config_error("connection '" + from + "' -> '" + to + "' already exists");
            }
        }
        snapshot();
        g.connections.push_back({from, to, replay});
    }

    void disconnect(const std::string& group_path, std::size_t index) {
        runtime::group_node& g = require_group(group_path);
        if (index >= g.connections.size()) {
            throw runtime::config_error("no connection #" + std::to_string(index) + " in group '" + group_path + "'");
        }
        snapshot();
        g.connections.erase(g.connections.begin() + static_cast<std::ptrdiff_t>(index));
    }

    void set_params(const std::string& group_path, const std::string& name, const std::string& params) {
        runtime::group_node& g = require_group(group_path);
        runtime::child_node* child = detail::find_child(g, name);
        if (child == nullptr || !child->module) {
            throw runtime::config_error("no module '" + name + "' in group '" + group_path + "'");
        }
        std::string canonical = params.empty() ? std::string{} : parse_params(params);
        snapshot();
        child->module->params = std::move(canonical);
    }

    void set_expose_input(const std::string& group_path, const std::string& alias, const std::string& port_path) {
        runtime::group_node& g = require_group(group_path);
        set_expose(g.expose_inputs, g, group_path, alias, port_path);
    }
    void set_expose_output(const std::string& group_path, const std::string& alias, const std::string& port_path) {
        runtime::group_node& g = require_group(group_path);
        set_expose(g.expose_outputs, g, group_path, alias, port_path);
    }
    void remove_expose_input(const std::string& group_path, const std::string& alias) {
        remove_expose(require_group(group_path).expose_inputs, group_path, alias);
    }
    void remove_expose_output(const std::string& group_path, const std::string& alias) {
        remove_expose(require_group(group_path).expose_outputs, group_path, alias);
    }

    void add_thread(const std::string& name, thread_mode mode, std::chrono::milliseconds period = {}) {
        detail::check_name(name, "thread name");
        for (const runtime::thread_node& t : cfg_.threads) {
            if (t.name == name) {
                throw runtime::config_error("duplicate thread name '" + name + "'");
            }
        }
        // те же контракты, что у pipeline_runner::add_thread
        if (mode == thread_mode::throttled && period <= std::chrono::milliseconds::zero()) {
            throw runtime::config_error("throttled thread '" + name + "' requires a positive period");
        }
        if (mode != thread_mode::throttled && period != std::chrono::milliseconds::zero()) {
            throw runtime::config_error("thread '" + name + "': period is only for throttled mode");
        }
        snapshot();
        cfg_.threads.push_back({name, mode, period});
    }

    void remove_thread(const std::string& name) {
        auto it = std::ranges::find_if(cfg_.threads, [&](const runtime::thread_node& t) { return t.name == name; });
        if (it == cfg_.threads.end()) {
            throw runtime::config_error("no thread '" + name + "'");
        }
        snapshot();
        cfg_.threads.erase(it);
        std::erase_if(cfg_.assignments, [&](const auto& a) { return a.second == name; });
    }

    void set_assignment(const std::string& group_path, const std::string& thread) {
        if (group_path.empty() || detail::find_group(cfg_.pipeline, group_path) == nullptr) {
            throw runtime::config_error("no group at path '" + group_path + "'");
        }
        if (std::ranges::none_of(cfg_.threads, [&](const runtime::thread_node& t) { return t.name == thread; })) {
            throw runtime::config_error("no thread '" + thread + "'");
        }
        snapshot();
        for (auto& [path, existing] : cfg_.assignments) {
            if (path == group_path) {
                existing = thread;
                return;
            }
        }
        cfg_.assignments.emplace_back(group_path, thread);
    }

    void clear_assignment(const std::string& group_path) {
        snapshot();
        std::erase_if(cfg_.assignments, [&](const auto& a) { return a.first == group_path; });
    }

    // Путь DLL в списке plugins конфига (формат пути — забота вызывающего:
    // GUI приводит к относительному от каталога конфига, где может).
    void add_plugin(const std::string& path) {
        if (path.empty()) {
            throw runtime::config_error("plugin path must not be empty");
        }
        if (std::ranges::find(cfg_.plugins, path) != cfg_.plugins.end()) {
            return;  // уже есть — не операция, историю не трогаем
        }
        snapshot();
        cfg_.plugins.push_back(path);
    }

    void remove_plugin(const std::string& path) {
        if (std::ranges::find(cfg_.plugins, path) == cfg_.plugins.end()) {
            throw runtime::config_error("no plugin '" + path + "' in config");
        }
        snapshot();
        std::erase(cfg_.plugins, path);
    }

    [[nodiscard]] bool can_undo() const {
        return !undo_.empty();
    }
    bool undo() {
        if (undo_.empty()) {
            return false;
        }
        redo_.push_back(runtime::encode(cfg_));
        cfg_ = runtime::decode(undo_.back());
        undo_.pop_back();
        return true;
    }
    [[nodiscard]] bool can_redo() const {
        return !redo_.empty();
    }
    bool redo() {
        if (redo_.empty()) {
            return false;
        }
        undo_.push_back(runtime::encode(cfg_));
        cfg_ = runtime::decode(redo_.back());
        redo_.pop_back();
        return true;
    }

    [[nodiscard]] std::optional<node_position> position(const std::string& node_path) const {
        auto it = positions_.find(node_path);
        return it == positions_.end() ? std::nullopt : std::optional(it->second);
    }

    void set_position(const std::string& node_path, node_position p) {
        positions_[node_path] = p;
    }

   private:
    document() = default;

    // Снапшот — только ПОСЛЕ всех проверок операции: отказанная операция
    // не должна ни менять документ, ни расти в истории.
    void snapshot() {
        undo_.push_back(runtime::encode(cfg_));
        redo_.clear();
    }

    [[nodiscard]] runtime::group_node& require_group(const std::string& path) {
        runtime::group_node* g = detail::find_group(cfg_.pipeline, path);
        if (g == nullptr) {
            throw runtime::config_error("no group at path '" + path + "'");
        }
        return *g;
    }

    void require_free_name(const runtime::group_node& g, const std::string& name) const {
        for (const runtime::child_node& c : g.children) {
            if (detail::child_name(c) == name) {
                throw runtime::config_error("duplicate child name '" + name + "'");
            }
        }
    }

    // Путь порта обязан вести к существующему ребёнку группы; сам порт
    // проверит сборка (философия платформы: реестр — на этапе build).
    void require_port_child(runtime::group_node& g, const std::string& group_path, const std::string& port_path) {
        const std::string child = detail::port_path_child(port_path);
        if (detail::find_child(g, child) == nullptr) {
            throw runtime::config_error("no child '" + child + "' in group '" + group_path + "' for '" + port_path + "'");
        }
    }

    // params хранится каноничным компактным дампом — тем же, что у decode.
    [[nodiscard]] static std::string parse_params(const std::string& params) {
        try {
            return nlohmann::json::parse(params).dump();
        } catch (const nlohmann::json::parse_error& e) {
            throw runtime::config_error(std::string("params is not valid JSON: ") + e.what());
        }
    }

    [[nodiscard]] static std::string join_path(const std::string& group_path, const std::string& name) {
        return group_path.empty() ? name : group_path + "." + name;
    }

    static void rewrite_full_path(std::string& path, const std::string& old_full, const std::string& new_full) {
        if (path == old_full) {
            path = new_full;
        } else if (path.starts_with(old_full + ".")) {
            path = new_full + path.substr(old_full.size());
        }
    }

    void set_expose(std::vector<std::pair<std::string, std::string>>& map,
                    runtime::group_node& g,
                    const std::string& group_path,
                    const std::string& alias,
                    const std::string& port_path) {
        detail::check_name(alias, "alias");
        require_port_child(g, group_path, port_path);
        snapshot();
        for (auto& [existing, path] : map) {
            if (existing == alias) {
                path = port_path;
                return;
            }
        }
        map.emplace_back(alias, port_path);
    }

    void remove_expose(std::vector<std::pair<std::string, std::string>>& map,
                       const std::string& group_path,
                       const std::string& alias) {
        const auto before = map.size();
        snapshot();
        std::erase_if(map, [&](const auto& e) { return e.first == alias; });
        if (map.size() == before) {
            undo_.pop_back();  // ничего не изменилось — снапшот лишний
            throw runtime::config_error("no alias '" + alias + "' in group '" + group_path + "'");
        }
    }

    // Sidecar рядом с конфигом: сам конфиг остаётся байт-в-байт пригодным
    // для atp_app.
    [[nodiscard]] static std::filesystem::path layout_path(std::filesystem::path file) {
        file.replace_extension(".layout.json");
        return file;
    }

    void load_layout(const std::filesystem::path& file) {
        std::ifstream in(file);
        if (!in) {
            return;  // нет sidecar — позиции даст автораскладка (layout.hpp)
        }
        try {
            const nlohmann::json doc = nlohmann::json::parse(in);
            // items() держит ссылку на объект: временный от value() умер бы
            // до тела цикла — поэтому через именованную переменную.
            const nlohmann::json positions = doc.value("positions", nlohmann::json::object());
            for (const auto& [path, p] : positions.items()) {
                if (p.is_object() && p.contains("x") && p.contains("y")) {
                    positions_[path] = {p.at("x").get<float>(), p.at("y").get<float>()};
                }
            }
        } catch (const nlohmann::json::parse_error&) {  // NOLINT(bugprone-empty-catch)
            // битый sidecar — не причина не открыть документ
        }
    }

    void save_layout(const std::filesystem::path& file) const {
        nlohmann::json positions = nlohmann::json::object();
        for (const auto& [path, p] : positions_) {
            positions[path] = {{"x", p.x}, {"y", p.y}};
        }
        nlohmann::json doc;
        doc["positions"] = std::move(positions);
        std::ofstream out(file);
        if (out) {
            out << doc.dump(4) << '\n';  // sidecar — best effort: конфиг уже сохранён
        }
    }

    runtime::config cfg_;
    bool had_includes_ = false;
    std::map<std::string, node_position> positions_;
    std::vector<nlohmann::json> undo_, redo_;
};

}  // namespace atp::studio

#endif  // ATP_STUDIO_DOCUMENT_HPP
