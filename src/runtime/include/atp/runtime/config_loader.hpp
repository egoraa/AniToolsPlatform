// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_RUNTIME_CONFIG_LOADER_HPP
#define ATP_RUNTIME_CONFIG_LOADER_HPP

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include <atp/config/node.hpp>
#include <atp/runtime/config_error.hpp>
#include <atp/runtime/config_model.hpp>
#include <atp/runtime/json_codec.hpp>
#include <atp/runtime/utf8_path.hpp>

namespace atp::runtime {

namespace detail {

inline constexpr std::size_t max_include_depth = 16;

inline atp::config::node read_json(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw config_error("cannot open config file '" + path_to_utf8(path) + "'");
    }
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    try {
        return json_parse(text);
    } catch (const config_error& e) {
        throw config_error("cannot parse '" + path_to_utf8(path) + "': " + e.what());
    }
}

inline void expand_includes(atp::config::node& node,
                            const std::filesystem::path& dir,
                            std::vector<std::filesystem::path>& stack);

inline atp::config::node load_fragment(const std::filesystem::path& path, std::vector<std::filesystem::path>& stack) {
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path);
    if (std::ranges::find(stack, canonical) != stack.end()) {
        std::string chain;
        for (const std::filesystem::path& p : stack) {
            chain += path_to_utf8(p) + " -> ";
        }
        throw config_error("include cycle: " + chain + path_to_utf8(canonical));
    }
    if (stack.size() >= max_include_depth) {
        throw config_error("include depth exceeds " + std::to_string(max_include_depth) + " at '" +
                           path_to_utf8(canonical) + "'");
    }
    stack.push_back(canonical);
    atp::config::node fragment = read_json(canonical);
    expand_includes(fragment, canonical.parent_path(), stack);
    stack.pop_back();
    return fragment;
}

inline void expand_includes(atp::config::node& node,
                            const std::filesystem::path& dir,
                            std::vector<std::filesystem::path>& stack) {
    if (node.is_object()) {
        if (const atp::config::node* include = node.find("$include"); include != nullptr) {
            if (node.size() != 1) {
                throw config_error("$include must be the only key of its object");
            }
            if (!include->is_string()) {
                throw config_error("$include path must be a string");
            }
            const std::string named = include->as_string();
            atp::config::node fragment = load_fragment(dir / path_from_utf8(named), stack);
            if (fragment.is_object() && fragment.find("version") != nullptr) {
                throw config_error("'version' is allowed only in the root config, found in included '" + named + "'");
            }
            node = std::move(fragment);
            return;
        }
        for (std::size_t i = 0; i < node.size(); ++i) {
            expand_includes(node[i], dir, stack);
        }
    } else if (node.is_array()) {
        for (std::size_t i = 0; i < node.size(); ++i) {
            expand_includes(node[i], dir, stack);
        }
    }
}

}  // namespace detail

/// Reads a config, expanding {"$include": "path.json"} anywhere in the tree. Include paths are
/// relative to the including file, and both cycles and depth are guarded.
/// @return a plain document — the validator and decode know nothing about includes
/// @throws config_error if a file cannot be read or parsed, or on a cycle or too deep a nesting
[[nodiscard]] inline atp::config::node load_config(const std::filesystem::path& path) {
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path);
    std::vector<std::filesystem::path> stack{canonical};
    atp::config::node doc = detail::read_json(canonical);
    detail::expand_includes(doc, canonical.parent_path(), stack);
    return doc;
}

}  // namespace atp::runtime

#endif
