#ifndef ATP_STUDIO_LAYOUT_HPP
#define ATP_STUDIO_LAYOUT_HPP

#include <cstddef>
#include <string>
#include <unordered_map>

#include <atp/app/config_model.hpp>
#include <atp/studio/document.hpp>

namespace atp::studio {

// Шаг сетки автораскладки; подобраны под типовой размер узла ImGui.
inline constexpr float layout_column_width = 260.0f;
inline constexpr float layout_row_height = 140.0f;

// Автораскладка одного уровня группы (канвас показывает уровень за раз):
// слой ребёнка — длиннейший путь по связям от истоков, колонка — слой,
// строка — порядковый номер в слое. Ключ результата — имя ребёнка.
[[nodiscard]] inline std::unordered_map<std::string, node_position> auto_layout(const app::group_node& g) {
    std::unordered_map<std::string, std::size_t> layer;
    for (const app::child_node& c : g.children) {
        layer[c.module ? c.module->name : c.group->name] = 0;
    }
    // Итерации с потолком в размер графа: цикл связей не зациклит раскладку,
    // а честной топологической сортировки редактору чернового вида не нужно.
    for (std::size_t pass = 0; pass < g.children.size(); ++pass) {
        bool changed = false;
        for (const app::connection_node& c : g.connections) {
            const std::string from = c.from.substr(0, c.from.find('.'));
            const std::string to = c.to.substr(0, c.to.find('.'));
            auto f = layer.find(from);
            auto t = layer.find(to);
            if (f != layer.end() && t != layer.end() && t->second < f->second + 1 &&
                f->second + 1 < g.children.size() + 1) {
                t->second = f->second + 1;
                changed = true;
            }
        }
        if (!changed) {
            break;
        }
    }
    std::unordered_map<std::size_t, std::size_t> row;
    std::unordered_map<std::string, node_position> out;
    for (const app::child_node& c : g.children) {
        const std::string& name = c.module ? c.module->name : c.group->name;
        const std::size_t l = layer[name];
        out[name] = {static_cast<float>(l) * layout_column_width, static_cast<float>(row[l]++) * layout_row_height};
    }
    return out;
}

}  // namespace atp::studio

#endif  // ATP_STUDIO_LAYOUT_HPP
