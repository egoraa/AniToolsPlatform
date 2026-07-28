#ifndef ATP_STUDIO_DOCUMENT_HPP
#define ATP_STUDIO_DOCUMENT_HPP

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <atp/runtime/config_loader.hpp>
#include <atp/runtime/config_model.hpp>
#include <atp/runtime/config_validator.hpp>

namespace atp::studio {

/// Position of a node on the canvas: editor metadata that never reaches the pipeline config and
/// lives in a sidecar file instead.
struct node_position {
    float x = 0.0f;
    float y = 0.0f;

    friend bool operator==(const node_position&, const node_position&) = default;
};

namespace detail {

// Descends a group path ("" is the root, then dot-separated segments). A module on the way or an
// unknown segment yields nullptr, leaving it to the caller to decide whether that is an error or
// merely an existence check.
inline const runtime::group_node* find_group(const runtime::group_node& root, const std::string& path) {
    const runtime::group_node* current = &root;
    std::size_t begin = 0;
    while (!path.empty()) {
        const std::size_t dot = path.find('.', begin);
        const std::string segment = path.substr(begin, dot == std::string::npos ? dot : dot - begin);
        const runtime::group_node* next = nullptr;
        for (const runtime::child_node& c : current->modules) {
            if (c.group && c.group->name == segment) {
                next = c.group.get();
                break;
            }
        }
        if (next == nullptr) {
            return nullptr;
        }
        current = next;
        if (dot == std::string::npos) {
            break;
        }
        begin = dot + 1;
    }
    return current;
}

inline runtime::group_node* find_group(runtime::group_node& root, const std::string& path) {
    // The const version is the only implementation; dropping const is legitimate here, since the
    // original object is not const.
    return const_cast<runtime::group_node*>(find_group(std::as_const(root), path));
}

// A name (of a child, a thread, an alias): non-empty and free of dots, the dot being the path
// separator.
inline void check_name(const std::string& name, const char* what) {
    if (name.empty() || name.find('.') != std::string::npos) {
        throw runtime::config_error(std::string(what) + " '" + name + "' must be non-empty and contain no '.'");
    }
}

// A "child.port" path: exactly one dot with both halves non-empty.
inline std::string port_path_child(const std::string& path) {
    const std::size_t dot = path.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 == path.size() ||
        path.find('.', dot + 1) != std::string::npos) {
        throw runtime::config_error("expected '<child>.<port>', got '" + path + "'");
    }
    return path.substr(0, dot);
}

inline const std::string& child_name(const runtime::child_node& c) {
    return c.module ? c.module->name : c.group->name;
}

inline runtime::child_node* find_child(runtime::group_node& g, const std::string& name) {
    for (runtime::child_node& c : g.modules) {
        if (child_name(c) == name) {
            return &c;
        }
    }
    return nullptr;
}

/// Name for a new child of @p g: @p base, with a numeric suffix if the name is taken. A null group
/// yields the base name — reporting a missing group is the caller's operation, not the name's.
[[nodiscard]] inline std::string unique_child_name(const runtime::group_node* g, const std::string& base) {
    auto taken = [&](const std::string& name) {
        return g != nullptr &&
               std::ranges::any_of(g->modules, [&](const runtime::child_node& c) { return child_name(c) == name; });
    };
    if (!taken(base)) {
        return base;
    }
    for (int i = 2;; ++i) {
        const std::string candidate = base + "_" + std::to_string(i);
        if (!taken(candidate)) {
            return candidate;
        }
    }
}

// Rewrites the "old." prefix of a port path when a child is renamed.
inline void rewrite_port_prefix(std::string& port_path, const std::string& old_name, const std::string& new_name) {
    if (port_path.starts_with(old_name + ".")) {
        port_path = new_name + port_path.substr(old_name.size());
    }
}

/// Full path of a node: "group.child", or just the name at the root. It lives here rather than in
/// the Qt layer, because non-Qt code operates on node positions too.
[[nodiscard]] inline std::string full_path(const std::string& group_path, const std::string& child) {
    return group_path.empty() ? child : group_path + "." + child;
}

}  // namespace detail

/// An editable document: the typed config model plus editor metadata. Every editing operation
/// checks its invariants and pushes a snapshot onto the undo stack; node positions are a visual
/// layer outside undo.
class document {
   public:
    /// Creates an empty document with the current schema version.
    [[nodiscard]] static document create() {
        document d;
        d.cfg_.schema = runtime::config_schema_version;
        return d;
    }

    /// Opens a config together with its layout sidecar.
    /// @throws runtime::config_error if the file cannot be read or fails validation; every
    ///         validation error is aggregated into that one exception, so the caller needs no
    ///         separate channel for the list
    [[nodiscard]] static document open(const std::filesystem::path& file) {
        document d;
        {
            // Includes are flattened on save, which is worth warning about honestly. The marker is
            // searched for in the raw text, since after expansion the file boundaries are gone.
            std::ifstream in(file);
            std::stringstream raw;
            raw << in.rdbuf();
            d.had_includes_ = raw.str().find("\"$include\"") != std::string::npos;
        }
        const nlohmann::json doc = runtime::load_config(file);
        const std::vector<std::string> errors = runtime::validate(doc);
        if (!errors.empty()) {
            std::string message = "invalid config '" + file.string() + "':";
            for (const std::string& e : errors) {
                message += "\n  " + e;
            }
            throw runtime::config_error(message);
        }
        d.cfg_ = runtime::decode(doc);
        d.load_layout(layout_path(file));
        return d;
    }

    /// Writes the config in canonical form and its layout sidecar next to it.
    /// @throws runtime::config_error if the config file cannot be written
    void save(const std::filesystem::path& file) const {
        std::ofstream out(file);
        if (!out) {
            throw runtime::config_error("cannot write config '" + file.string() + "'");
        }
        out << runtime::encode(cfg_).dump(4) << '\n';
        save_layout(layout_path(file));
    }

    /// The edited config model.
    [[nodiscard]] const runtime::config& config() const {
        return cfg_;
    }

    /// Whether the opened file used $include, which saving would flatten.
    [[nodiscard]] bool had_includes() const {
        return had_includes_;
    }

    /// Group at a path ("" is the root); nullptr if there is none.
    [[nodiscard]] const runtime::group_node* group_at(const std::string& path) const {
        return detail::find_group(cfg_.pipeline, path);
    }

    /// Adds a module to a group.
    /// @param group_path group to add to ("" is the root)
    /// @param factory factory name in the registry
    /// @param name child name; defaults to the factory name
    /// @param factory_version factory version; absent means the latest registered one
    /// @throws runtime::config_error if the group is missing or the name is bad or already taken
    void add_module(const std::string& group_path,
                    const std::string& factory,
                    std::string name = {},
                    std::optional<version> factory_version = {}) {
        runtime::group_node& g = require_group(group_path);
        if (name.empty()) {
            name = factory;  // the same default decode applies
        }
        detail::check_name(name, "module name");
        require_free_name(g, name);
        snapshot();
        runtime::child_node c;
        c.module = runtime::module_node{factory, std::move(name), factory_version, {}};
        g.modules.push_back(std::move(c));
    }

    /// Adds an empty subgroup to a group.
    /// @throws runtime::config_error if the parent group is missing or the name is bad or taken
    void add_group(const std::string& group_path, const std::string& name) {
        runtime::group_node& g = require_group(group_path);
        detail::check_name(name, "group name");
        require_free_name(g, name);
        snapshot();
        runtime::child_node c;
        c.group = std::make_unique<runtime::group_node>();
        c.group->name = name;
        g.modules.push_back(std::move(c));
    }

    /// Removes a child together with everything that referenced it: connections, exported ports,
    /// thread assignments and canvas positions.
    /// @throws runtime::config_error if the group or the child is missing
    void remove_child(const std::string& group_path, const std::string& name) {
        runtime::group_node& g = require_group(group_path);
        if (detail::find_child(g, name) == nullptr) {
            throw runtime::config_error("no child '" + name + "' in group '" + group_path + "'");
        }
        snapshot();
        const std::string prefix = name + ".";
        std::erase_if(g.connections, [&](const runtime::connection_node& c) {
            return c.from.starts_with(prefix) || c.to.starts_with(prefix);
        });
        std::erase_if(g.expose_inputs, [&](const auto& e) { return e.second.starts_with(prefix); });
        std::erase_if(g.expose_outputs, [&](const auto& e) { return e.second.starts_with(prefix); });
        const std::string full = join_path(group_path, name);
        std::erase_if(cfg_.assignments,
                      [&](const auto& a) { return a.first == full || a.first.starts_with(full + "."); });
        std::erase_if(g.modules, [&](const runtime::child_node& c) { return detail::child_name(c) == name; });
        // Positions are a visual layer, but there is no point accumulating orphaned keys.
        std::erase_if(positions_, [&](const auto& p) { return p.first == full || p.first.starts_with(full + "."); });
    }

    /// Renames a child, rewriting every path that referenced it: connections, exported ports,
    /// thread assignments and canvas positions.
    /// @throws runtime::config_error if the group or the child is missing, or the new name is bad
    ///         or already taken
    void rename_child(const std::string& group_path, const std::string& old_name, const std::string& new_name) {
        runtime::group_node& g = require_group(group_path);
        runtime::child_node* child = detail::find_child(g, old_name);
        if (child == nullptr) {
            throw runtime::config_error("no child '" + old_name + "' in group '" + group_path + "'");
        }
        detail::check_name(new_name, "child name");
        if (new_name != old_name) {
            require_free_name(g, new_name);
        }
        snapshot();
        if (child->module) {
            child->module->name = new_name;
        } else {
            child->group->name = new_name;
        }
        for (runtime::connection_node& c : g.connections) {
            detail::rewrite_port_prefix(c.from, old_name, new_name);
            detail::rewrite_port_prefix(c.to, old_name, new_name);
        }
        for (auto& [alias, path] : g.expose_inputs) {
            detail::rewrite_port_prefix(path, old_name, new_name);
        }
        for (auto& [alias, path] : g.expose_outputs) {
            detail::rewrite_port_prefix(path, old_name, new_name);
        }
        const std::string old_full = join_path(group_path, old_name);
        const std::string new_full = join_path(group_path, new_name);
        for (auto& [path, thread] : cfg_.assignments) {
            rewrite_full_path(path, old_full, new_full);
        }
        std::map<std::string, node_position> renamed;
        for (auto& [path, p] : positions_) {
            std::string key = path;
            rewrite_full_path(key, old_full, new_full);
            renamed[key] = p;
        }
        positions_ = std::move(renamed);
    }

    /// Records a connection between two "child.port" paths within a group.
    /// @throws runtime::config_error if the group or a referenced child is missing, a path is
    ///         malformed, or the same connection already exists
    void connect(const std::string& group_path, const std::string& from, const std::string& to, bool replay = false) {
        runtime::group_node& g = require_group(group_path);
        require_port_child(g, group_path, from);
        require_port_child(g, group_path, to);
        for (const runtime::connection_node& c : g.connections) {
            if (c.from == from && c.to == to) {
                throw runtime::config_error("connection '" + from + "' -> '" + to + "' already exists");
            }
        }
        snapshot();
        g.connections.push_back({from, to, replay});
    }

    /// Removes the connection at @p index in a group's connection list.
    /// @throws runtime::config_error if the group is missing or the index is out of range
    void disconnect(const std::string& group_path, std::size_t index) {
        runtime::group_node& g = require_group(group_path);
        if (index >= g.connections.size()) {
            throw runtime::config_error("no connection #" + std::to_string(index) + " in group '" + group_path + "'");
        }
        snapshot();
        g.connections.erase(g.connections.begin() + static_cast<std::ptrdiff_t>(index));
    }

    /// Sets a property value in the document. Editing a live pipeline goes through a separate
    /// channel (session); the document is the source for saving and for the next run.
    /// @param group_path group holding the module ("" is the root)
    /// @param name module name within that group
    /// @param prop property name
    /// @param value a scalar, as the validator contract requires
    /// @throws runtime::config_error if the value is not a scalar, or the group or module is
    ///         missing
    void set_property(const std::string& group_path,
                      const std::string& name,
                      const std::string& prop,
                      nlohmann::json value) {
        if (!value.is_number() && !value.is_string() && !value.is_boolean()) {
            throw runtime::config_error("property '" + prop + "' must be a scalar (number, string or boolean)");
        }
        runtime::module_node& m = require_module(group_path, name);
        snapshot();
        for (auto& [existing, v] : m.properties) {
            if (existing == prop) {
                v = std::move(value);
                return;
            }
        }
        m.properties.emplace_back(prop, std::move(value));
    }

    /// Drops a property value, which means "go back to the module's default". A missing entry is
    /// not an error — the reset button is idempotent — and a snapshot is pushed only on a real
    /// change.
    /// @throws runtime::config_error if the group or the module is missing
    void clear_property(const std::string& group_path, const std::string& name, const std::string& prop) {
        runtime::module_node& m = require_module(group_path, name);
        const auto it = std::ranges::find_if(m.properties, [&](const auto& p) { return p.first == prop; });
        if (it == m.properties.end()) {
            return;
        }
        snapshot();
        m.properties.erase(it);
    }

    /// Exports a child input under a group alias, replacing an alias of the same name.
    /// @throws runtime::config_error if the group or the referenced child is missing, or a name is
    ///         malformed
    void set_expose_input(const std::string& group_path, const std::string& alias, const std::string& port_path) {
        runtime::group_node& g = require_group(group_path);
        set_expose(g.expose_inputs, g, group_path, alias, port_path);
    }

    /// Exports a child output under a group alias, replacing an alias of the same name.
    /// @throws runtime::config_error if the group or the referenced child is missing, or a name is
    ///         malformed
    void set_expose_output(const std::string& group_path, const std::string& alias, const std::string& port_path) {
        runtime::group_node& g = require_group(group_path);
        set_expose(g.expose_outputs, g, group_path, alias, port_path);
    }

    /// Removes an exported input alias.
    /// @throws runtime::config_error if the group or the alias is missing
    void remove_expose_input(const std::string& group_path, const std::string& alias) {
        remove_expose(require_group(group_path).expose_inputs, group_path, alias);
    }

    /// Removes an exported output alias.
    /// @throws runtime::config_error if the group or the alias is missing
    void remove_expose_output(const std::string& group_path, const std::string& alias) {
        remove_expose(require_group(group_path).expose_outputs, group_path, alias);
    }

    /// Declares a runner thread.
    /// @throws runtime::config_error on a bad or duplicate name, or if the period contradicts the
    ///         mode
    void add_thread(const std::string& name, thread_mode mode, std::chrono::milliseconds period = {}) {
        detail::check_name(name, "thread name");
        for (const runtime::thread_node& t : cfg_.threads) {
            if (t.name == name) {
                throw runtime::config_error("duplicate thread name '" + name + "'");
            }
        }
        // The same contracts as pipeline_runner::add_thread.
        if (mode == thread_mode::throttled && period <= std::chrono::milliseconds::zero()) {
            throw runtime::config_error("throttled thread '" + name + "' requires a positive period");
        }
        if (mode != thread_mode::throttled && period != std::chrono::milliseconds::zero()) {
            throw runtime::config_error("thread '" + name + "': period is only for throttled mode");
        }
        snapshot();
        cfg_.threads.push_back({name, mode, period});
    }

    /// Removes a thread along with every assignment pointing at it.
    /// @throws runtime::config_error if there is no such thread
    void remove_thread(const std::string& name) {
        auto it = std::ranges::find_if(cfg_.threads, [&](const runtime::thread_node& t) { return t.name == name; });
        if (it == cfg_.threads.end()) {
            throw runtime::config_error("no thread '" + name + "'");
        }
        snapshot();
        cfg_.threads.erase(it);
        std::erase_if(cfg_.assignments, [&](const auto& a) { return a.second == name; });
    }

    /// Changes the pacing of a declared thread. Unlike a remove/add pair this keeps the thread's
    /// identity, so the groups assigned to it stay assigned.
    /// @param name name of the declared thread
    /// @param mode new mode
    /// @param period new period, positive for throttled and zero for the other modes
    /// @throws runtime::config_error if there is no such thread, or the period contradicts the mode
    void set_thread(const std::string& name, thread_mode mode, std::chrono::milliseconds period = {}) {
        auto it = std::ranges::find_if(cfg_.threads, [&](const runtime::thread_node& t) { return t.name == name; });
        if (it == cfg_.threads.end()) {
            throw runtime::config_error("no thread '" + name + "'");
        }
        // The same contracts as add_thread and pipeline_runner::add_thread.
        if (mode == thread_mode::throttled && period <= std::chrono::milliseconds::zero()) {
            throw runtime::config_error("throttled thread '" + name + "' requires a positive period");
        }
        if (mode != thread_mode::throttled && period != std::chrono::milliseconds::zero()) {
            throw runtime::config_error("thread '" + name + "': period is only for throttled mode");
        }
        snapshot();
        it->mode = mode;
        it->period = period;
    }

    /// Assigns a group to a thread, replacing any previous assignment of that group.
    /// @throws runtime::config_error if the group path or the thread is unknown
    void set_assignment(const std::string& group_path, const std::string& thread) {
        if (group_path.empty() || detail::find_group(cfg_.pipeline, group_path) == nullptr) {
            throw runtime::config_error("no group at path '" + group_path + "'");
        }
        if (std::ranges::none_of(cfg_.threads, [&](const runtime::thread_node& t) { return t.name == thread; })) {
            throw runtime::config_error("no thread '" + thread + "'");
        }
        snapshot();
        for (auto& [path, existing] : cfg_.assignments) {
            if (path == group_path) {
                existing = thread;
                return;
            }
        }
        cfg_.assignments.emplace_back(group_path, thread);
    }

    /// Drops the thread assignment of a group, letting it run inline in its ancestor again.
    void clear_assignment(const std::string& group_path) {
        snapshot();
        std::erase_if(cfg_.assignments, [&](const auto& a) { return a.first == group_path; });
    }

    /// Adds a plugin path to the config's plugin list. The path format is the caller's business —
    /// the GUI makes it relative to the config directory where it can. Adding a path that is
    /// already there is not an operation and leaves the history alone.
    /// @throws runtime::config_error on an empty path
    void add_plugin(const std::string& path) {
        if (path.empty()) {
            throw runtime::config_error("plugin path must not be empty");
        }
        if (std::ranges::find(cfg_.plugins, path) != cfg_.plugins.end()) {
            return;
        }
        snapshot();
        cfg_.plugins.push_back(path);
    }

    /// Removes a plugin path from the config.
    /// @throws runtime::config_error if the config lists no such plugin
    void remove_plugin(const std::string& path) {
        if (std::ranges::find(cfg_.plugins, path) == cfg_.plugins.end()) {
            throw runtime::config_error("no plugin '" + path + "' in config");
        }
        snapshot();
        std::erase(cfg_.plugins, path);
    }

    /// Whether there is an operation to undo.
    [[nodiscard]] bool can_undo() const {
        return !undo_.empty();
    }

    /// Reverts the last editing operation.
    /// @return false if the history is empty
    bool undo() {
        if (undo_.empty()) {
            return false;
        }
        redo_.push_back(runtime::encode(cfg_));
        cfg_ = runtime::decode(undo_.back());
        undo_.pop_back();
        return true;
    }
    /// Whether there is an undone operation to redo.
    [[nodiscard]] bool can_redo() const {
        return !redo_.empty();
    }

    /// Reapplies the last undone operation.
    /// @return false if there is nothing to redo
    bool redo() {
        if (redo_.empty()) {
            return false;
        }
        undo_.push_back(runtime::encode(cfg_));
        cfg_ = runtime::decode(redo_.back());
        redo_.pop_back();
        return true;
    }

    /// Canvas position of a node; nullopt if it has none yet.
    [[nodiscard]] std::optional<node_position> position(const std::string& node_path) const {
        auto it = positions_.find(node_path);
        return it == positions_.end() ? std::nullopt : std::optional(it->second);
    }

    /// Records a node's canvas position. Positions live outside undo.
    void set_position(const std::string& node_path, node_position p) {
        positions_[node_path] = p;
    }

   private:
    document() = default;

    // A snapshot is taken only AFTER every check of an operation: a rejected operation must neither
    // change the document nor grow the history.
    void snapshot() {
        undo_.push_back(runtime::encode(cfg_));
        redo_.clear();
    }

    [[nodiscard]] runtime::group_node& require_group(const std::string& path) {
        runtime::group_node* g = detail::find_group(cfg_.pipeline, path);
        if (g == nullptr) {
            throw runtime::config_error("no group at path '" + path + "'");
        }
        return *g;
    }

    [[nodiscard]] runtime::module_node& require_module(const std::string& group_path, const std::string& name) {
        runtime::group_node& g = require_group(group_path);
        runtime::child_node* child = detail::find_child(g, name);
        if (child == nullptr || !child->module) {
            throw runtime::config_error("no module '" + name + "' in group '" + group_path + "'");
        }
        return *child->module;
    }

    void require_free_name(const runtime::group_node& g, const std::string& name) const {
        for (const runtime::child_node& c : g.modules) {
            if (detail::child_name(c) == name) {
                throw runtime::config_error("duplicate child name '" + name + "'");
            }
        }
    }

    // A port path has to lead to an existing child of the group; the port itself is checked when
    // building, which is where the registry becomes available.
    void require_port_child(runtime::group_node& g, const std::string& group_path, const std::string& port_path) {
        const std::string child = detail::port_path_child(port_path);
        if (detail::find_child(g, child) == nullptr) {
            throw runtime::config_error("no child '" + child + "' in group '" + group_path + "' for '" + port_path +
                                        "'");
        }
    }

    [[nodiscard]] static std::string join_path(const std::string& group_path, const std::string& name) {
        return group_path.empty() ? name : group_path + "." + name;
    }

    static void rewrite_full_path(std::string& path, const std::string& old_full, const std::string& new_full) {
        if (path == old_full) {
            path = new_full;
        } else if (path.starts_with(old_full + ".")) {
            path = new_full + path.substr(old_full.size());
        }
    }

    void set_expose(std::vector<std::pair<std::string, std::string>>& map,
                    runtime::group_node& g,
                    const std::string& group_path,
                    const std::string& alias,
                    const std::string& port_path) {
        detail::check_name(alias, "alias");
        require_port_child(g, group_path, port_path);
        snapshot();
        for (auto& [existing, path] : map) {
            if (existing == alias) {
                path = port_path;
                return;
            }
        }
        map.emplace_back(alias, port_path);
    }

    void remove_expose(std::vector<std::pair<std::string, std::string>>& map,
                       const std::string& group_path,
                       const std::string& alias) {
        const auto before = map.size();
        snapshot();
        std::erase_if(map, [&](const auto& e) { return e.first == alias; });
        if (map.size() == before) {
            undo_.pop_back();  // nothing changed — the snapshot is superfluous
            throw runtime::config_error("no alias '" + alias + "' in group '" + group_path + "'");
        }
    }

    // A sidecar next to the config, so the config itself stays byte-for-byte usable by atp_app.
    [[nodiscard]] static std::filesystem::path layout_path(std::filesystem::path file) {
        file.replace_extension(".layout.json");
        return file;
    }

    void load_layout(const std::filesystem::path& file) {
        std::ifstream in(file);
        if (!in) {
            return;  // no sidecar — the positions come from the auto layout instead
        }
        try {
            const nlohmann::json doc = nlohmann::json::parse(in);
            // items() holds a reference to the object, and the temporary from value() would die
            // before the loop body — hence the named variable.
            const nlohmann::json positions = doc.value("positions", nlohmann::json::object());
            for (const auto& [path, p] : positions.items()) {
                if (p.is_object() && p.contains("x") && p.contains("y")) {
                    positions_[path] = {p.at("x").get<float>(), p.at("y").get<float>()};
                }
            }
        } catch (const nlohmann::json::parse_error&) {  // NOLINT(bugprone-empty-catch)
            // A corrupt sidecar is no reason to refuse opening the document.
        }
    }

    void save_layout(const std::filesystem::path& file) const {
        nlohmann::json positions = nlohmann::json::object();
        for (const auto& [path, p] : positions_) {
            positions[path] = {{"x", p.x}, {"y", p.y}};
        }
        nlohmann::json doc;
        doc["positions"] = std::move(positions);
        std::ofstream out(file);
        if (out) {
            out << doc.dump(4) << '\n';  // best effort: the config itself is already saved
        }
    }

    runtime::config cfg_;
    bool had_includes_ = false;
    std::map<std::string, node_position> positions_;
    std::vector<nlohmann::json> undo_, redo_;
};

}  // namespace atp::studio

#endif  // ATP_STUDIO_DOCUMENT_HPP
