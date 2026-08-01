#ifndef ATP_RUNTIME_CONFIG_LOADER_HPP
#define ATP_RUNTIME_CONFIG_LOADER_HPP

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <atp/runtime/config_model.hpp>

namespace atp::runtime {

namespace detail {

inline constexpr std::size_t max_include_depth = 16;

inline nlohmann::json read_json(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw config_error("cannot open config file '" + path.string() + "'");
    }
    try {
        return nlohmann::json::parse(file);
    } catch (const nlohmann::json::parse_error& e) {
        throw config_error("cannot parse '" + path.string() + "': " + e.what());
    }
}

inline void expand_includes(nlohmann::json& node,
                            const std::filesystem::path& dir,
                            std::vector<std::filesystem::path>& stack);

inline nlohmann::json load_fragment(const std::filesystem::path& path, std::vector<std::filesystem::path>& stack) {
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path);
    if (std::ranges::find(stack, canonical) != stack.end()) {
        std::string chain;
        for (const std::filesystem::path& p : stack) {
            chain += p.string() + " -> ";
        }
        throw config_error("include cycle: " + chain + canonical.string());
    }
    if (stack.size() >= max_include_depth) {
        throw config_error("include depth exceeds " + std::to_string(max_include_depth) + " at '" + canonical.string() +
                           "'");
    }
    stack.push_back(canonical);
    nlohmann::json fragment = read_json(canonical);
    expand_includes(fragment, canonical.parent_path(), stack);
    stack.pop_back();
    return fragment;
}

inline void expand_includes(nlohmann::json& node,
                            const std::filesystem::path& dir,
                            std::vector<std::filesystem::path>& stack) {
    if (node.is_object()) {
        if (node.contains("$include")) {
            if (node.size() != 1) {
                throw config_error("$include must be the only key of its object");
            }
            if (!node.at("$include").is_string()) {
                throw config_error("$include path must be a string");
            }
            nlohmann::json fragment = load_fragment(dir / node.at("$include").get<std::string>(), stack);
            if (fragment.is_object() && fragment.contains("version")) {
                throw config_error("'version' is allowed only in the root config, found in included '" +
                                   node.at("$include").get<std::string>() + "'");
            }
            node = std::move(fragment);
            return;
        }
        for (auto& [key, value] : node.items()) {
            expand_includes(value, dir, stack);
        }
    } else if (node.is_array()) {
        for (nlohmann::json& element : node) {
            expand_includes(element, dir, stack);
        }
    }
}

}  // namespace detail

/// Reads a config, expanding {"$include": "path.json"} anywhere in the tree. Include paths are
/// relative to the including file, and both cycles and depth are guarded.
/// @return a plain JSON document — the validator and decode know nothing about includes
/// @throws config_error if a file cannot be read or parsed, or on a cycle or too deep a nesting
[[nodiscard]] inline nlohmann::json load_config(const std::filesystem::path& path) {
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path);
    std::vector<std::filesystem::path> stack{canonical};
    nlohmann::json doc = detail::read_json(canonical);
    detail::expand_includes(doc, canonical.parent_path(), stack);
    return doc;
}

}  // namespace atp::runtime

#endif
