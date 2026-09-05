// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_PROJECT_FROM_DESCRIPTION_HPP
#define ATP_STUDIO_PROJECT_FROM_DESCRIPTION_HPP

#include <cstddef>
#include <optional>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include <atp/config/node.hpp>
#include <atp/runtime/config_model.hpp>
#include <atp/runtime/config_value_json.hpp>
#include <atp/runtime/json_codec.hpp>
#include <atp/studio/layout.hpp>
#include <atp/studio/project.hpp>

namespace atp::studio {

namespace detail {

/// The "modules" array a dotted path belongs in, creating nothing along the way: describe_pipeline
/// lists a parent before its children, so by the time a child arrives its group is already there.
/// @param root the root group object being built
/// @param path dotted path of the node whose parent is wanted; a bare name means the root
[[nodiscard]] inline atp::config::node& parent_modules_of(atp::config::node& root, const std::string& path) {
    atp::config::node* current = &root;
    std::size_t begin = 0;
    while (true) {
        const std::size_t dot = path.find('.', begin);
        if (dot == std::string::npos) {
            return (*current)["modules"];
        }
        const std::string segment = path.substr(begin, dot - begin);
        atp::config::node& siblings = (*current)["modules"];
        for (std::size_t i = 0; i < siblings.size(); ++i) {
            atp::config::node& child = siblings[i];
            const atp::config::node* group = child.find("group");
            if (group != nullptr && group->is_string() && group->as_string() == segment) {
                current = &child;
                break;
            }
        }
        begin = dot + 1;
    }
}

/// Last segment of a dotted path — the node's own name within its group.
[[nodiscard]] inline std::string leaf_of(const std::string& path) {
    const std::size_t dot = path.rfind('.');
    return dot == std::string::npos ? path : path.substr(dot + 1);
}

/// The property value as a config scalar. A description carries values as strings, because that is
/// the only form property_base speaks; the kind is what says whether the string was a number.
[[nodiscard]] inline atp::config::node scalar_of(const nlohmann::json& property) {
    auto text = property.at("value").get<std::string>();
    if (property.at("kind").get<std::string>() == "text") {
        return {std::move(text)};
    }
    if (std::optional<atp::config::node> parsed = runtime::try_json_parse(text)) {
        return *std::move(parsed);
    }
    return {std::move(text)};
}

/// Properties worth writing down: the ones that differ from their default. A value equal to the
/// default cannot be told apart from one that was never set, and writing every property out would
/// turn a five-line config into a hundred.
[[nodiscard]] inline atp::config::node properties_of(const nlohmann::json& described) {
    atp::config::node out(atp::config::node::object_type{});
    for (const nlohmann::json& p : described.at("properties")) {
        if (p.at("value") != p.at("default")) {
            out[p.at("name").get<std::string>()] = scalar_of(p);
        }
    }
    return out;
}

/// Copies a subtree of the description across, if the description has one under @p key and it is not
/// empty. The description is a protocol value and the document is not, so this is where the two meet.
inline void carry_over(atp::config::node& into, const nlohmann::json& from, const char* key) {
    if (from.contains(key) && !from.at(key).empty()) {
        into[key] = runtime::to_config_value(from.at(key));
    }
}

/// Lays every level of the tree out, one group at a time — which is what the canvas shows.
inline void layout_group(project& p, const runtime::group_node& g, const std::string& path) {
    for (const auto& [name, position] : auto_layout(g)) {
        p.set_position(path.empty() ? name : path + "." + name, position);
    }
    for (const runtime::child_node& c : g.modules) {
        if (c.group) {
            layout_group(p, *c.group, path.empty() ? c.group->name : path + "." + c.group->name);
        }
    }
}

}  // namespace detail

/// Turns a describe_pipeline result into an editable project — the studio's mirror of a pipeline it
/// does not own.
///
/// The document goes through the ordinary validator on the way in, deliberately: if a remote
/// pipeline does not fold back into a valid config, that is worth learning at once and with a
/// message, rather than by drawing half a graph.
///
/// It is built as a config::node rather than assembled with the document library and converted at the
/// end. The description arriving from the wire is the only JSON here, and it crosses into the
/// platform's own tree the moment a piece of it is used, which is the same boundary every other
/// reader of a document keeps.
///
/// What the mirror cannot carry, because the runtime keeps none of it in a readable form: the
/// plugins the modules came from and the thread layout. A mirror saved to disk is therefore a
/// description of the graph, not the config the host was started from.
/// @param described the structuredContent of describe_pipeline
/// @throws runtime::config_error if the description does not produce a valid config
[[nodiscard]] inline project project_from_description(const nlohmann::json& described) {
    atp::config::node root(atp::config::node::object_type{});
    root["modules"] = atp::config::node(atp::config::node::array_type{});
    detail::carry_over(root, described, "connections");
    detail::carry_over(root, described, "expose");

    for (const nlohmann::json& described_node : described.at("modules")) {
        const auto path = described_node.at("path").get<std::string>();
        atp::config::node& siblings = detail::parent_modules_of(root, path);
        if (described_node.at("group").get<bool>()) {
            atp::config::node group(atp::config::node::object_type{});
            group["group"] = atp::config::node(detail::leaf_of(path));
            group["modules"] = atp::config::node(atp::config::node::array_type{});
            detail::carry_over(group, described_node, "connections");
            detail::carry_over(group, described_node, "expose");
            siblings.push_back(std::move(group));
            continue;
        }
        atp::config::node module(atp::config::node::object_type{});
        module["module"] = runtime::to_config_value(described_node.at("module"));
        module["name"] = atp::config::node(detail::leaf_of(path));
        atp::config::node properties = detail::properties_of(described_node);
        if (properties.size() != 0) {
            module["properties"] = std::move(properties);
        }
        siblings.push_back(std::move(module));
    }

    atp::config::node doc(atp::config::node::object_type{});
    doc["version"] = atp::config::node(runtime::config_schema_version.to_string());
    doc["pipeline"] = std::move(root);

    project mirror = project::from_document(doc);
    detail::layout_group(mirror, mirror.config().pipeline, "");
    return mirror;
}

}  // namespace atp::studio

#endif
