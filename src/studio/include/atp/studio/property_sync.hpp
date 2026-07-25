#ifndef ATP_STUDIO_PROPERTY_SYNC_HPP
#define ATP_STUDIO_PROPERTY_SYNC_HPP

#include <string>

#include <nlohmann/json.hpp>

#include <atp/group.hpp>
#include <atp/runtime/config_model.hpp>
#include <atp/studio/document.hpp>

namespace atp::studio {

namespace detail {

// Строка проперти → JSON-скаляр по kind: числа и bool возвращаются в
// конфиг своим типом, не строкой (обратная сторона scalar_to_string
// builder'а). to_string кодека канонична — parse не откажет; text — как есть.
// Ограниченность набором на тип записи не влияет: перечисление из чисел
// остаётся числом, из имён enum — строкой.
[[nodiscard]] inline nlohmann::json property_value_to_json(const io::property_base& p) {
    switch (p.kind()) {
        case io::property_kind::number:
            return nlohmann::json::parse(p.to_string());
        case io::property_kind::boolean:
            return nlohmann::json(p.to_string() == "true");
        case io::property_kind::text:
            break;
    }
    return nlohmann::json(p.to_string());
}

inline void sync_group(document& doc,
                       const runtime::group_node& node,
                       const group& live,
                       const std::string& group_path) {
    for (const runtime::child_node& c : node.children) {
        if (c.module) {
            const module_base* m = live.find_module(c.module->name);
            if (m == nullptr) {
                continue;  // рассинхрон документа и запуска — не повод падать при сохранении
            }
            for (const io::property_base* p : m->properties().owned()) {
                if (!p->persistent()) {
                    continue;  // transient в документ не попадает никогда
                }
                if (p->to_string() == p->default_string()) {
                    doc.clear_property(group_path, c.module->name, p->name());
                } else {
                    doc.set_property(group_path, c.module->name, p->name(), property_value_to_json(*p));
                }
            }
        } else {
            const group* sub = live.find_group(c.group->name);
            if (sub != nullptr) {
                sync_group(doc, *c.group, *sub, group_path.empty() ? c.group->name : group_path + "." + c.group->name);
            }
        }
    }
}

}  // namespace detail

// Перед сохранением на ходу: значения persistent-пропертей живых модулей —
// в документ (модуль мог поменять их и сам). Равное дефолту — из документа
// вон: конфиг не обрастает шумом. Каждая правка пишет undo-снапшот, и это
// допустимо: операция редкая, ровно перед сохранением.
inline void sync_persistent_properties(document& doc, const runtime::config& cfg, const group& root) {
    detail::sync_group(doc, cfg.pipeline, root, "");
}

}  // namespace atp::studio

#endif  // ATP_STUDIO_PROPERTY_SYNC_HPP
