#ifndef ATP_STUDIO_DOCUMENT_HPP
#define ATP_STUDIO_DOCUMENT_HPP

#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <atp/app/config_loader.hpp>
#include <atp/app/config_model.hpp>
#include <atp/app/config_validator.hpp>

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
inline const app::group_node* find_group(const app::group_node& root, const std::string& path) {
    const app::group_node* current = &root;
    std::size_t begin = 0;
    while (!path.empty()) {
        const std::size_t dot = path.find('.', begin);
        const std::string segment = path.substr(begin, dot == std::string::npos ? dot : dot - begin);
        const app::group_node* next = nullptr;
        for (const app::child_node& c : current->children) {
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

inline app::group_node* find_group(app::group_node& root, const std::string& path) {
    // const-версия — единственная реализация; снятие const законно:
    // исходный объект неконстантен.
    return const_cast<app::group_node*>(find_group(std::as_const(root), path));
}

}  // namespace detail

// Редактируемый документ: типизированная модель конфига + editor-метаданные.
// Все операции редактирования проверяют инварианты и пишут снапшот в undo;
// позиции — визуальный слой вне undo.
class document {
   public:
    [[nodiscard]] static document create() {
        document d;
        d.cfg_.schema = app::config_schema_version;
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
        const nlohmann::json doc = app::load_config(file);
        const std::vector<std::string> errors = app::validate(doc);
        if (!errors.empty()) {
            std::string message = "invalid config '" + file.string() + "':";
            for (const std::string& e : errors) {
                message += "\n  " + e;
            }
            throw app::config_error(message);
        }
        d.cfg_ = app::decode(doc);
        d.load_layout(layout_path(file));
        return d;
    }

    void save(const std::filesystem::path& file) const {
        std::ofstream out(file);
        if (!out) {
            throw app::config_error("cannot write config '" + file.string() + "'");
        }
        out << app::encode(cfg_).dump(4) << '\n';
        save_layout(layout_path(file));
    }

    [[nodiscard]] const app::config& config() const {
        return cfg_;
    }

    [[nodiscard]] bool had_includes() const {
        return had_includes_;
    }

    [[nodiscard]] const app::group_node* group_at(const std::string& path) const {
        return detail::find_group(cfg_.pipeline, path);
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
            for (const auto& [path, p] : doc.value("positions", nlohmann::json::object()).items()) {
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

    app::config cfg_;
    bool had_includes_ = false;
    std::map<std::string, node_position> positions_;
};

}  // namespace atp::studio

#endif  // ATP_STUDIO_DOCUMENT_HPP
