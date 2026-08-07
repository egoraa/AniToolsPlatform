// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_POSITION_FILE_HPP
#define ATP_STUDIO_POSITION_FILE_HPP

#include <filesystem>
#include <fstream>
#include <map>
#include <string>

#include <nlohmann/json.hpp>

#include <atp/studio/node_position.hpp>

/// @file
/// The `.layout.json` sidecar: where canvas positions are read from and written to. Not to be
/// confused with layout.hpp, which computes positions — this only moves them between a map and a
/// file. Both reads are deliberately forgiving: a project whose sidecar is missing or corrupt opens
/// with its nodes unplaced, because losing the arrangement of a diagram is not a reason to refuse
/// the diagram.
namespace atp::studio {

[[nodiscard]] inline std::filesystem::path layout_sidecar_path(std::filesystem::path config_file) {
    config_file.replace_extension(".layout.json");
    return config_file;
}

inline void load_positions(const std::filesystem::path& file, std::map<std::string, node_position>& positions) {
    std::ifstream in(file);
    if (!in) {
        return;
    }
    try {
        const nlohmann::json doc = nlohmann::json::parse(in);
        const nlohmann::json stored = doc.value("positions", nlohmann::json::object());
        for (const auto& [path, p] : stored.items()) {
            if (p.is_object() && p.contains("x") && p.contains("y")) {
                positions[path] = {p.at("x").get<float>(), p.at("y").get<float>()};
            }
        }
    } catch (const nlohmann::json::parse_error&) {  // NOLINT(bugprone-empty-catch)
    }
}

inline void save_positions(const std::filesystem::path& file, const std::map<std::string, node_position>& positions) {
    nlohmann::json stored = nlohmann::json::object();
    for (const auto& [path, p] : positions) {
        stored[path] = {{"x", p.x}, {"y", p.y}};
    }
    nlohmann::json doc;
    doc["positions"] = std::move(stored);
    std::ofstream out(file);
    if (out) {
        out << doc.dump(4) << '\n';
    }
}

}  // namespace atp::studio

#endif
