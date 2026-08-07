// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_PROJECT_FROM_DESCRIPTION_HPP
#define ATP_STUDIO_PROJECT_FROM_DESCRIPTION_HPP

#include <cstddef>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include <atp/runtime/config_model.hpp>
#include <atp/studio/layout.hpp>
#include <atp/studio/project.hpp>

namespace atp::studio {

namespace detail {

/// The "modules" array a dotted path belongs in, creating nothing along the way: describe_pipeline
/// lists a parent before its children, so by the time a child arrives its group is already there.
/// @param root the root group object being built
/// @param path dotted path of the node whose parent is wanted; a bare name means the root
[[nodiscard]] inline nlohmann::json& parent_modules_of(nlohmann::json& root, const std::string& path) {
    nlohmann::json* current = &root;
    std::size_t begin = 0;
    while (true) {
        const std::size_t dot = path.find('.', begin);
        if (dot == std::string::npos) {
            return (*current)["modules"];
        }
        const std::string segment = path.substr(begin, dot - begin);
        for (nlohmann::json& child : (*current)["modules"]) {
            if (child.contains("group") && child.at("group") == segment) {
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
[[nodiscard]] inline nlohmann::json scalar_of(const nlohmann::json& property) {
    auto text = property.at("value").get<std::string>();
    if (property.at("kind").get<std::string>() == "text") {
        return text;
    }
    try {
        return nlohmann::json::parse(text);
    } catch (const nlohmann::json::parse_error&) {
        return text;
    }
}

/// Properties worth writing down: the ones that differ from their default. A value equal to the
/// default cannot be told apart from one that was never set, and writing every property out would
/// turn a five-line config into a hundred.
[[nodiscard]] inline nlohmann::json properties_of(const nlohmann::json& node) {
    nlohmann::json out = nlohmann::json::object();
    for (const nlohmann::json& p : node.at("properties")) {
        if (p.at("value") != p.at("default")) {
            out[p.at("name").get<std::string>()] = scalar_of(p);
        }
    }
    return out;
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
/// What the mirror cannot carry, because the runtime keeps none of it in a readable form: the
/// plugins the modules came from and the thread layout. A mirror saved to disk is therefore a
/// description of the graph, not the config the host was started from.
/// @param described the structuredContent of describe_pipeline
/// @throws runtime::config_error if the description does not produce a valid config
[[nodiscard]] inline project project_from_description(const nlohmann::json& described) {
    nlohmann::json root{{"modules", nlohmann::json::array()}};
    if (described.contains("connections") && !described.at("connections").empty()) {
        root["connections"] = described.at("connections");
    }
    if (described.contains("expose") && !described.at("expose").empty()) {
        root["expose"] = described.at("expose");
    }

    for (const nlohmann::json& node : described.at("modules")) {
        const auto path = node.at("path").get<std::string>();
        nlohmann::json& siblings = detail::parent_modules_of(root, path);
        if (node.at("group").get<bool>()) {
            nlohmann::json group{{"group", detail::leaf_of(path)}, {"modules", nlohmann::json::array()}};
            if (node.contains("connections") && !node.at("connections").empty()) {
                group["connections"] = node.at("connections");
            }
            if (node.contains("expose") && !node.at("expose").empty()) {
                group["expose"] = node.at("expose");
            }
            siblings.push_back(std::move(group));
            continue;
        }
        nlohmann::json module{{"module", node.at("module")}, {"name", detail::leaf_of(path)}};
        nlohmann::json properties = detail::properties_of(node);
        if (!properties.empty()) {
            module["properties"] = std::move(properties);
        }
        siblings.push_back(std::move(module));
    }

    project mirror = project::from_document(
        nlohmann::json{{"version", runtime::config_schema_version.to_string()}, {"pipeline", std::move(root)}});
    detail::layout_group(mirror, mirror.config().pipeline, "");
    return mirror;
}

}  // namespace atp::studio

#endif
