#ifndef ATP_STUDIO_PORT_TYPES_HPP
#define ATP_STUDIO_PORT_TYPES_HPP

#include <any>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <typeindex>

#include <atp/studio/document.hpp>
#include <atp/studio/module_manager.hpp>
#include <atp/version.hpp>

namespace atp::studio {

// Описатель фабрики по имени и версии; nullptr — фабрика не загружена.
// Коллбэк, а не module_manager напрямую: GUI отдаёт кэшированный describe
// (пробный экземпляр на каждую перестройку сцены слишком дорог).
using describe_fn = std::function<const module_info*(const std::string&, const std::optional<version>&)>;

// Тип порта по пути "дитя.порт" внутри группы: модуль — из describe,
// подгруппа — рекурсивно сквозь её expose до реального порта. nullopt —
// фабрика не загружена/порт не найден.
[[nodiscard]] inline std::optional<std::type_index> resolve_port_type(const app::group_node& g,
                                                                      const std::string& port_path,
                                                                      bool output,
                                                                      const describe_fn& describe) {
    const std::size_t dot = port_path.find('.');
    if (dot == std::string::npos) {
        return std::nullopt;
    }
    const std::string child = port_path.substr(0, dot);
    const std::string port = port_path.substr(dot + 1);
    for (const app::child_node& c : g.children) {
        if (c.module && c.module->name == child) {
            const module_info* info = describe(c.module->factory, c.module->factory_version);
            if (info == nullptr) {
                return std::nullopt;
            }
            const auto& list = output ? info->outputs : info->inputs;
            for (const port_info& p : list) {
                if (p.name == port) {
                    return p.type;
                }
            }
            return std::nullopt;
        }
        if (c.group && c.group->name == child) {
            const auto& expose = output ? c.group->expose_outputs : c.group->expose_inputs;
            for (const auto& [alias, path] : expose) {
                if (alias == port) {
                    return resolve_port_type(*c.group, path, output, describe);
                }
            }
            return std::nullopt;
        }
    }
    return std::nullopt;
}

// Правило совместимости — то же, что в рантайме (io::input::accepts):
// точное совпадение типов либо универсальный input<std::any>. Обратное
// (output<std::any> в типизированный вход) намеренно не поддержано.
[[nodiscard]] inline bool types_compatible(std::type_index produced, std::type_index accepted) {
    return accepted == produced || accepted == std::type_index(typeid(std::any));
}

// Связь с проверкой типов до записи в документ. Тип неизвестен хотя бы с
// одной стороны (плагин не загружен) — пропускаем: запрещать связь по
// незнанию хуже, чем дать рантайму отказать при запуске.
inline void connect_ports(document& doc,
                          const std::string& group_path,
                          const std::string& from,
                          const std::string& to,
                          const describe_fn& describe,
                          bool replay = false) {
    const app::group_node* g = doc.group_at(group_path);
    if (g == nullptr) {
        throw app::config_error("no group '" + group_path + "'");
    }
    const auto produced = resolve_port_type(*g, from, true, describe);
    const auto accepted = resolve_port_type(*g, to, false, describe);
    if (produced && accepted && !types_compatible(*produced, *accepted)) {
        throw app::config_error("incompatible types: output '" + from + "' is " + produced->name() + ", input '" + to +
                                "' accepts " + accepted->name());
    }
    doc.connect(group_path, from, to, replay);
}

}  // namespace atp::studio

#endif  // ATP_STUDIO_PORT_TYPES_HPP
