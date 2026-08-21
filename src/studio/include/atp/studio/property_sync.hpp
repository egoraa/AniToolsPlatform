// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_PROPERTY_SYNC_HPP
#define ATP_STUDIO_PROPERTY_SYNC_HPP

#include <optional>
#include <string>

#include <atp/config/node.hpp>
#include <atp/runtime/config_model.hpp>
#include <atp/runtime/group.hpp>
#include <atp/runtime/json_codec.hpp>
#include <atp/studio/project.hpp>

namespace atp::studio {

namespace detail {

[[nodiscard]] inline atp::config::node property_value_of(const io::property_base& p) {
    switch (p.kind()) {
        case io::property_kind::number:
            if (const std::optional<atp::config::node> parsed = runtime::try_json_parse(p.to_string())) {
                return *parsed;
            }
            break;
        case io::property_kind::boolean:
            return atp::config::node(p.to_string() == "true");
        case io::property_kind::text:
            break;
    }
    return atp::config::node(p.to_string());
}

inline void sync_group(project& proj,
                       const runtime::group_node& node,
                       const runtime::group& live,
                       const std::string& group_path) {
    for (const runtime::child_node& c : node.modules) {
        if (c.module) {
            const runtime::module_node& declared = *c.module;
            const module_base* m = live.find_module(declared.name);
            if (m == nullptr) {
                continue;
            }
            for (const io::property_base* p : m->properties().owned()) {
                if (!p->persistent()) {
                    continue;
                }
                if (p->to_string() == p->default_string()) {
                    proj.clear_property(group_path, declared.name, p->name());
                } else {
                    proj.set_property(group_path, declared.name, p->name(), property_value_of(*p));
                }
            }
        } else {
            const runtime::group* sub = live.find_group(c.group->name);
            if (sub != nullptr) {
                sync_group(proj, *c.group, *sub, group_path.empty() ? c.group->name : group_path + "." + c.group->name);
            }
        }
    }
}

}  // namespace detail

/// Pulls the persistent property values of the live modules into the project before saving on the
/// fly — a module may have changed them on its own. A value equal to the default is dropped from
/// the project instead, keeping the config free of noise. Every edit pushes an undo snapshot,
/// which is acceptable for an operation this rare.
inline void sync_persistent_properties(project& proj, const runtime::config& cfg, const runtime::group& root) {
    detail::sync_group(proj, cfg.pipeline, root, "");
}

}  // namespace atp::studio

#endif
