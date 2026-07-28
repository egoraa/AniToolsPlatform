#ifndef ATP_STUDIO_PROPERTY_SYNC_HPP
#define ATP_STUDIO_PROPERTY_SYNC_HPP

#include <string>

#include <nlohmann/json.hpp>

#include <atp/group.hpp>
#include <atp/runtime/config_model.hpp>
#include <atp/studio/document.hpp>

namespace atp::studio {

namespace detail {

// Property string → JSON scalar, picked by the kind: numbers and booleans go back into the config
// as their own type rather than as strings (the reverse of the builder's scalar_to_string). The
// codec's to_string is canonical, so parse cannot fail. Being constrained by a value set does not
// affect the written type: an enumeration of numbers stays a number, one of enum names a string.
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
    for (const runtime::child_node& c : node.modules) {
        if (c.module) {
            const module_base* m = live.find_module(c.module->name);
            if (m == nullptr) {
                continue;  // a document out of step with the run is no reason to fail the save
            }
            for (const io::property_base* p : m->properties().owned()) {
                if (!p->persistent()) {
                    continue;  // a transient value never reaches the document
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

/// Pulls the persistent property values of the live modules into the document before saving on the
/// fly — a module may have changed them on its own. A value equal to the default is dropped from
/// the document instead, keeping the config free of noise. Every edit pushes an undo snapshot,
/// which is acceptable for an operation this rare.
inline void sync_persistent_properties(document& doc, const runtime::config& cfg, const group& root) {
    detail::sync_group(doc, cfg.pipeline, root, "");
}

}  // namespace atp::studio

#endif  // ATP_STUDIO_PROPERTY_SYNC_HPP
