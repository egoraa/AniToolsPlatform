#ifndef ATP_STUDIO_THREAD_RESOLVE_HPP
#define ATP_STUDIO_THREAD_RESOLVE_HPP

#include <cstddef>
#include <string>
#include <vector>

#include <atp/runtime/config_model.hpp>

namespace atp::studio {

/// Thread a group path ends up running on, plus how that was decided.
struct resolved_thread {
    std::string name;
    bool inherited = false;
};

/// Resolves the thread of a group path the way pipeline_runner does: an assignment on the path
/// itself wins, otherwise the nearest assigned ancestor, otherwise the first declared thread (which
/// is where an unassigned root goes), and with no threads declared at all the implicit "main".
/// A module has no assignment of its own — pass the path of the group holding it.
/// @param cfg config the threads and assignments are read from
/// @param group_path path of the group in question; empty means the root
/// @return the thread name and whether it came from somewhere above
[[nodiscard]] inline resolved_thread resolve_thread(const runtime::config& cfg, const std::string& group_path) {
    for (const auto& [path, thread] : cfg.assignments) {
        if (path == group_path) {
            return {thread, false};
        }
    }
    std::string ancestor = group_path;
    while (true) {
        const std::size_t dot = ancestor.rfind('.');
        if (dot == std::string::npos) {
            break;
        }
        ancestor.resize(dot);
        for (const auto& [path, thread] : cfg.assignments) {
            if (path == ancestor) {
                return {thread, true};
            }
        }
    }
    return {cfg.threads.empty() ? std::string("main") : cfg.threads.front().name, true};
}

/// Group paths assigned to a thread, in the order the assignments are declared.
/// @param cfg config the assignments are read from
/// @param thread thread name to collect the groups of
/// @return the assigned group paths, empty when nothing points at the thread
[[nodiscard]] inline std::vector<std::string> groups_on_thread(const runtime::config& cfg, const std::string& thread) {
    std::vector<std::string> result;
    for (const auto& [path, name] : cfg.assignments) {
        if (name == thread) {
            result.push_back(path);
        }
    }
    return result;
}

}  // namespace atp::studio

#endif
