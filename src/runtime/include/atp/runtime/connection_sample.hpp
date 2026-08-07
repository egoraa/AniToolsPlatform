// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_RUNTIME_CONNECTION_SAMPLE_HPP
#define ATP_RUNTIME_CONNECTION_SAMPLE_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <atp/group.hpp>

namespace atp::runtime {

/// Monitoring sample of one connection. The (group path, index) pair matches the config's, since
/// build_group preserves declaration order — that is what lets a reader address a connection it saw
/// in the document.
///
/// The sample reports activity, not content: an output keeps no copy of what it wrote, so there is
/// nothing to read a value from without making every write pay for the reading.
struct connection_sample {
    std::string group_path;
    std::size_t index = 0;
    std::uint64_t writes = 0;
};

namespace detail {

inline void collect_connections(const group& g, const std::string& path, std::vector<connection_sample>& out) {
    std::size_t index = 0;
    for (const group::connection& c : g.connections()) {
        out.push_back({path, index, c.out->write_count()});
        ++index;
    }
    for (const group::child& child : g.children()) {
        if (const group* sub = child.subgroup) {
            collect_connections(*sub, path.empty() ? child.name : path + "." + child.name, out);
        }
    }
}

}  // namespace detail

/// Samples every connection of a tree: the last value that travelled it and how many writes it has
/// seen. A free function rather than a method of session, because two hosts need the same walk and
/// only one of them owns a session.
/// @param root group to walk, its subgroups included
/// @return one entry per connection, a group's own connections before those of its children
[[nodiscard]] inline std::vector<connection_sample> sample_connections(const group& root) {
    std::vector<connection_sample> out;
    detail::collect_connections(root, "", out);
    return out;
}

}  // namespace atp::runtime

#endif
