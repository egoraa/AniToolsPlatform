// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_RUNTIME_CONFIG_PATH_HPP
#define ATP_RUNTIME_CONFIG_PATH_HPP

#include <atp/config/access_error.hpp>
#include <atp/config/node.hpp>

#include <cstddef>
#include <string>
#include <string_view>

namespace atp::runtime {

namespace detail {

/// Where a lookup gave up, so at() can say it in one sentence and find() can ignore it.
struct path_stop {
    std::size_t resolved = 0;
    atp::config::kind kind = atp::config::kind::null;
    std::string_view missing_key;
    bool out_of_range = false;
    std::size_t index = 0;
    std::size_t size = 0;
};

[[nodiscard]] inline std::string path_message(std::string_view path, std::string_view what, std::size_t at) {
    return "config: bad path '" + std::string(path) + "' (" + std::string(what) + " at " + std::to_string(at) + ")";
}

[[nodiscard]] inline std::string stop_message(std::string_view path, const path_stop& stop) {
    const std::string where = stop.resolved == 0 ? "the root" : "'" + std::string(path.substr(0, stop.resolved)) + "'";
    const std::string prefix = "config: '" + std::string(path) + "' ";
    if (stop.out_of_range) {
        return prefix + "has no index " + std::to_string(stop.index) + " in " + where + " (size " +
               std::to_string(stop.size) + ")";
    }
    if (!stop.missing_key.empty()) {
        return prefix + "has no key '" + std::string(stop.missing_key) + "' in " + where;
    }
    return prefix + "stops at " + where + " (" + std::string(atp::config::node::kind_name(stop.kind)) + ")";
}

/// Parses @p path whole and resolves as far as the data allows.
///
/// Parsing deliberately does not stop where resolving does: a lookup that finds nothing only sets
/// @p stop and leaves the walk without a node, while the grammar is checked to the end of the
/// string. Otherwise the same malformed path would throw against one config and answer "not there"
/// against another, making the report of a typo depend on the data.
[[nodiscard]] inline const atp::config::node* walk_path(const atp::config::node& root,
                                                        std::string_view path,
                                                        path_stop* stop) {
    if (path.empty()) {
        throw atp::config::access_error(path_message(path, "empty", 0));
    }
    const atp::config::node* found = &root;
    std::size_t pos = 0;
    std::size_t resolved = 0;
    bool first = true;
    while (true) {
        const std::size_t name_begin = pos;
        while (pos < path.size() && path[pos] != '.' && path[pos] != '[') {
            ++pos;
        }
        const std::string_view name = path.substr(name_begin, pos - name_begin);
        const bool leading_index = first && pos < path.size() && path[pos] == '[';
        if (name.empty() && !leading_index) {
            throw atp::config::access_error(path_message(path, "empty key", name_begin));
        }
        if (!name.empty() && found != nullptr) {
            const atp::config::node* child = found->find(name);
            if (child == nullptr) {
                if (stop != nullptr) {
                    *stop = {resolved, found->kind(), found->is_object() ? name : std::string_view{}, false, 0, 0};
                }
                found = nullptr;
            } else {
                found = child;
                resolved = pos;
            }
        }
        while (pos < path.size() && path[pos] == '[') {
            const std::size_t bracket = pos;
            ++pos;
            const std::size_t digits = pos;
            std::size_t index = 0;
            while (pos < path.size() && path[pos] >= '0' && path[pos] <= '9') {
                const std::size_t digit = static_cast<std::size_t>(path[pos] - '0');
                if (index > (static_cast<std::size_t>(-1) - digit) / 10) {
                    throw atp::config::access_error(path_message(path, "index too large", digits));
                }
                index = (index * 10) + digit;
                ++pos;
            }
            if (pos == digits) {
                throw atp::config::access_error(path_message(path, "'[' without a number", bracket));
            }
            if (pos == path.size() || path[pos] != ']') {
                throw atp::config::access_error(path_message(path, "unclosed '['", bracket));
            }
            ++pos;
            if (found == nullptr) {
                continue;
            }
            if (!found->is_array() || index >= found->size()) {
                if (stop != nullptr) {
                    *stop = {resolved, found->kind(), {}, true, index, found->size()};
                }
                found = nullptr;
            } else {
                found = &(*found)[index];
                resolved = pos;
            }
        }
        if (pos == path.size()) {
            return found;
        }
        if (path[pos] != '.') {
            throw atp::config::access_error(path_message(path, "expected '.'", pos));
        }
        ++pos;
        if (pos == path.size()) {
            throw atp::config::access_error(path_message(path, "trailing '.'", pos - 1));
        }
        first = false;
    }
}

}  // namespace detail

/// Node at @p path within @p root, or nullptr when there is nothing there.
///
/// Grammar: `path := segment ('.' segment)*`, `segment := name index* | index+`,
/// `index := '[' digits ']'`. A nameless segment is legal only as the very first one, so "[0].rate"
/// addresses an array root while "a.[0]" is a mistake. A key containing '.' or '[' is unreachable this
/// way and stays reachable by walking the node itself.
///
/// **A malformed path throws rather than answering nullptr**, which is why this is not noexcept: the
/// path is written by the module's own author in their own source, and turning a typo into "nothing
/// there" turns it into an hour of debugging. nullptr is the other failure — absence.
///
/// A free function over a node rather than a member of a config, because both configs that carry a
/// tree need it and they reach module_config by different routes; the grammar is about a node and a
/// path and was never about who owned them.
/// @throws atp::config::access_error naming the path and the offset of whatever broke the grammar
[[nodiscard]] inline const atp::config::node* find_path(const atp::config::node& root, std::string_view path) {
    return detail::walk_path(root, path, nullptr);
}

/// @throws atp::config::access_error on a malformed path, or naming the full path and where the walk
///         stopped
[[nodiscard]] inline const atp::config::node& at_path(const atp::config::node& root, std::string_view path) {
    detail::path_stop stop;
    if (const atp::config::node* found = detail::walk_path(root, path, &stop)) {
        return *found;
    }
    throw atp::config::access_error(detail::stop_message(path, stop));
}

}  // namespace atp::runtime

#endif
