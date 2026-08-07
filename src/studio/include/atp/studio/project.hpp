// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_PROJECT_HPP
#define ATP_STUDIO_PROJECT_HPP

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
#include <atp/studio/clipboard.hpp>
#include <atp/studio/expose_cascade.hpp>
#include <atp/studio/node_lookup.hpp>
#include <atp/studio/node_position.hpp>
#include <atp/studio/node_ref.hpp>
#include <atp/studio/position_file.hpp>

namespace atp::studio {

/// What a move did to the project beyond relocating the node itself. The counters cover the whole
/// cascade, not just the source group: an export that lost its target takes with it whatever
/// referenced it further up.
struct move_result {
    std::string new_name;
    std::size_t dropped_connections;
    std::size_t dropped_exposes;
};

/// An editable project: the typed config model plus editor metadata. Every editing operation
/// checks its invariants and pushes a snapshot onto the undo stack; node positions are a visual
/// layer outside undo.
class project {
   public:
    /// Creates an empty project with the current schema version.
    [[nodiscard]] static project create() {
        project p;
        p.cfg_.schema = runtime::config_schema_version;
        p.saved_ = runtime::encode(p.cfg_);
        return p;
    }

    /// Builds a project from a config document already in memory. Shared with open(), and the entry
    /// point for a document nobody wrote to disk — the mirror of a remote pipeline.
    /// @param doc config document, schema 2.0
    /// @throws runtime::config_error with every validation error aggregated into one message
    [[nodiscard]] static project from_document(const nlohmann::json& doc) {
        const std::vector<std::string> errors = runtime::validate(doc);
        if (!errors.empty()) {
            std::string message = "invalid config:";
            for (const std::string& e : errors) {
                message += "\n  " + e;
            }
            throw runtime::config_error(message);
        }
        project p;
        p.cfg_ = runtime::decode(doc);
        p.saved_ = runtime::encode(p.cfg_);
        return p;
    }

    /// Opens a config together with its layout sidecar.
    /// @throws runtime::config_error if the file cannot be read or fails validation; every
    ///         validation error is aggregated into that one exception, so the caller needs no
    ///         separate channel for the list
    [[nodiscard]] static project open(const std::filesystem::path& file) {
        project p;
        {
            std::ifstream in(file);
            std::stringstream raw;
            raw << in.rdbuf();
            p.had_includes_ = raw.str().contains("\"$include\"");
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
        project opened = from_document(doc);
        opened.had_includes_ = p.had_includes_;
        load_positions(layout_sidecar_path(file), opened.positions_);
        return opened;
    }

    /// Writes the config in canonical form and its layout sidecar next to it.
    /// @throws runtime::config_error if the config file cannot be written
    void save(const std::filesystem::path& file) {
        std::ofstream out(file);
        if (!out) {
            throw runtime::config_error("cannot write config '" + file.string() + "'");
        }
        out << runtime::encode(cfg_).dump(4) << '\n';
        save_positions(layout_sidecar_path(file), positions_);
        saved_ = runtime::encode(cfg_);
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
            name = factory;
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

    /// Removes several children of one group, each together with everything that referenced it:
    /// connections, exported ports, thread assignments and canvas positions. The whole batch is a
    /// single undo step, which is what a multi-node delete in the editor needs. Every name is
    /// checked before anything is touched, so a bad one leaves the project alone instead of half
    /// deleted.
    /// @param group_path group holding the children ("" is the root)
    /// @param names children to remove; an empty list is not an operation
    /// @throws runtime::config_error if the group is missing or it has no child under some name
    void remove_children(const std::string& group_path, const std::vector<std::string>& names) {
        runtime::group_node& g = require_group(group_path);
        for (const std::string& name : names) {
            if (detail::find_child(g, name) == nullptr) {
                throw runtime::config_error("no child '" + name + "' in group '" + group_path + "'");
            }
        }
        if (names.empty()) {
            return;
        }
        const edit_scope scope(*this);
        for (const std::string& name : names) {
            remove_one_child(group_path, name);
        }
    }

    /// Removes a child together with everything that referenced it: connections, exported ports,
    /// thread assignments and canvas positions.
    /// @throws runtime::config_error if the group or the child is missing
    void remove_child(const std::string& group_path, const std::string& name) {
        remove_children(group_path, {name});
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
        const std::string old_full = node_ref{group_path, old_name}.full();
        const std::string new_full = node_ref{group_path, new_name}.full();
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

    /// Moves a child into another group. Connections and exported ports of the source group that
    /// referenced it are dropped — they are scoped to their group and cannot follow the node — and
    /// so is everything that referenced *those* exports further up (see the cascade in
    /// remove_expose_output), while thread assignments and canvas positions of the whole moved
    /// subtree are rewritten to the new path.
    /// @param from_group group currently holding the child ("" is the root)
    /// @param name child name within that group
    /// @param to_group group to move it into ("" is the root)
    /// @return the name the child got in the target group and what was dropped on the way
    /// @throws runtime::config_error if either group or the child is missing, the target is the
    ///         group the child already sits in, or a group is moved into its own subtree
    move_result move_child(const std::string& from_group, const std::string& name, const std::string& to_group) {
        runtime::group_node& from = require_group(from_group);
        if (detail::find_child(from, name) == nullptr) {
            throw runtime::config_error("no child '" + name + "' in group '" + from_group + "'");
        }
        if (from_group == to_group) {
            throw runtime::config_error("child '" + name + "' is already in group '" + to_group + "'");
        }
        const node_ref moved_ref{from_group, name};
        const std::string old_full = moved_ref.full();
        if (moved_ref.contains(to_group)) {
            throw runtime::config_error("cannot move '" + old_full + "' into its own subtree '" + to_group + "'");
        }
        runtime::group_node& to = require_group(to_group);

        move_result result;
        result.new_name = detail::unique_child_name(&to, name);
        snapshot();

        const std::string prefix = name + ".";
        const std::size_t connections_before = from.connections.size();
        std::erase_if(from.connections, [&](const runtime::connection_node& c) {
            return c.from.starts_with(prefix) || c.to.starts_with(prefix);
        });
        result.dropped_connections = connections_before - from.connections.size();

        lost_aliases lost = take_exports_of(from, prefix);
        result.dropped_exposes = lost.size();

        auto it = std::ranges::find_if(from.modules,
                                       [&](const runtime::child_node& c) { return detail::child_name(c) == name; });
        runtime::child_node moved = std::move(*it);
        from.modules.erase(it);
        if (moved.module) {
            moved.module->name = result.new_name;
        } else {
            moved.group->name = result.new_name;
        }
        to.modules.push_back(std::move(moved));

        const std::string new_full = node_ref{to_group, result.new_name}.full();
        for (auto& [path, thread] : cfg_.assignments) {
            rewrite_full_path(path, old_full, new_full);
        }
        std::map<std::string, node_position> moved_positions;
        for (auto& [path, p] : positions_) {
            std::string key = path;
            rewrite_full_path(key, old_full, new_full);
            moved_positions[key] = p;
        }
        positions_ = std::move(moved_positions);

        const cascade_count above = cascade_lost_aliases(cfg_.pipeline, from_group, std::move(lost));
        result.dropped_connections += above.connections;
        result.dropped_exposes += above.exposes;
        return result;
    }

    /// Copies a child into a group, itself and everything under it. A subgroup arrives with its
    /// whole subtree, and the connections and exported ports inside that subtree come along, since
    /// they name only things that were copied too. Connections of the *source group* that referenced
    /// the original are not reproduced: a copy arrives unconnected, because which of the original's
    /// neighbours the copy should also talk to is a question only the user can answer. Thread
    /// assignments and canvas positions of the subtree are duplicated under the new path, so the
    /// copy runs and is laid out like the original instead of silently becoming unassigned.
    /// @param from_group group holding the child ("" is the root)
    /// @param name child name within that group
    /// @param to_group group to copy it into ("" is the root); the same group duplicates in place
    /// @return the name the copy got in the target group, suffixed if the original's was taken
    /// @throws runtime::config_error if either group or the child is missing
    std::string copy_child(const std::string& from_group, const std::string& name, const std::string& to_group) {
        runtime::group_node& from = require_group(from_group);
        const runtime::child_node* source = detail::find_child(from, name);
        if (source == nullptr) {
            throw runtime::config_error("no child '" + name + "' in group '" + from_group + "'");
        }
        runtime::group_node& to = require_group(to_group);

        runtime::child_node copy = detail::clone_child(*source);
        const std::string new_name = detail::unique_child_name(&to, name);
        if (copy.module) {
            copy.module->name = new_name;
        } else {
            copy.group->name = new_name;
        }
        snapshot();
        to.modules.push_back(std::move(copy));

        const std::string old_full = node_ref{from_group, name}.full();
        const std::string new_full = node_ref{to_group, new_name}.full();
        std::vector<std::pair<std::string, std::string>> copied_assignments;
        for (const auto& [path, thread] : cfg_.assignments) {
            if (path == old_full || path.starts_with(old_full + ".")) {
                std::string moved_path = path;
                rewrite_full_path(moved_path, old_full, new_full);
                copied_assignments.emplace_back(std::move(moved_path), thread);
            }
        }
        cfg_.assignments.insert(cfg_.assignments.end(), copied_assignments.begin(), copied_assignments.end());

        std::map<std::string, node_position> copied_positions;
        for (const auto& [path, p] : positions_) {
            if (path == old_full || path.starts_with(old_full + ".")) {
                std::string key = path;
                rewrite_full_path(key, old_full, new_full);
                copied_positions[key] = p;
            }
        }
        positions_.insert(copied_positions.begin(), copied_positions.end());
        return new_name;
    }

    /// Detaches copies of the named children of a group: the children themselves, the connections
    /// that run between them, and the canvas positions and thread assignments of their subtrees. A
    /// connection with one end outside the selection is not taken — which of the original's
    /// neighbours the copy should also talk to is a question only the user can answer, the same
    /// reasoning copy_child follows. The project is not touched and no undo snapshot is pushed, so
    /// a plain copy costs the user no history.
    /// @param group_path group holding the children ("" is the root)
    /// @param names children to copy, in any order; the result keeps the group's own order, which
    ///        is what makes a paste reproduce the lifecycle order of the original
    /// @return the snapshot; empty if @p names is empty
    /// @throws runtime::config_error if the group is missing or it has no child under some name
    [[nodiscard]] clipboard copy_children(const std::string& group_path, const std::vector<std::string>& names) const {
        const runtime::group_node* g = detail::find_group(cfg_.pipeline, group_path);
        if (g == nullptr) {
            throw runtime::config_error("no group at path '" + group_path + "'");
        }
        const auto chosen = [&](const std::string& name) { return std::ranges::find(names, name) != names.end(); };
        for (const std::string& name : names) {
            if (std::ranges::none_of(g->modules,
                                     [&](const runtime::child_node& c) { return detail::child_name(c) == name; })) {
                throw runtime::config_error("no child '" + name + "' in group '" + group_path + "'");
            }
        }

        clipboard clip;
        for (const runtime::child_node& c : g->modules) {
            const std::string& name = detail::child_name(c);
            if (!chosen(name)) {
                continue;
            }
            clip_node entry;
            entry.node = detail::clone_child(c);
            const std::string full = node_ref{group_path, name}.full();
            entry.position = position(full);
            const std::string prefix = full + ".";
            for (const auto& [path, p] : positions_) {
                if (path.starts_with(prefix)) {
                    entry.subtree_positions[path.substr(prefix.size())] = p;
                }
            }
            for (const auto& [path, thread] : cfg_.assignments) {
                if (path == full) {
                    entry.assignments.emplace_back(std::string(), thread);
                } else if (path.starts_with(prefix)) {
                    entry.assignments.emplace_back(path.substr(prefix.size()), thread);
                }
            }
            clip.nodes.push_back(std::move(entry));
        }
        for (const runtime::connection_node& c : g->connections) {
            if (chosen(detail::port_path_child(c.from)) && chosen(detail::port_path_child(c.to))) {
                clip.connections.push_back(c);
            }
        }
        return clip;
    }

    /// Inserts clipboard content into a group as one undo step: the children under free names, the
    /// connections that ran between them rewritten to those names, and their canvas positions and
    /// thread assignments. An assignment naming a thread the config does not declare is skipped —
    /// the clipboard outlives the project it was filled from, so it may well arrive from another
    /// one; a module whose factory is not registered here is inserted as it is, and the editor shows
    /// it as such.
    /// @param group_path group to paste into ("" is the root)
    /// @param clip snapshot to insert; an empty one is not an operation and leaves history alone
    /// @param at position for the top-left corner of the pasted block, which keeps the relative
    ///        geometry of the selection; nullopt writes the stored positions as they are, which is
    ///        what a paste from outside the canvas wants
    /// @return the names the pasted nodes got, in insertion order
    /// @throws runtime::config_error if there is no group at @p group_path
    std::vector<std::string> paste(const std::string& group_path,
                                   const clipboard& clip,
                                   std::optional<node_position> at) {
        runtime::group_node& g = require_group(group_path);
        if (clip.empty()) {
            return {};
        }
        const edit_scope scope(*this);

        std::optional<node_position> origin;
        for (const clip_node& entry : clip.nodes) {
            if (!entry.position) {
                continue;
            }
            origin = origin
                         ? node_position{std::min(origin->x, entry.position->x), std::min(origin->y, entry.position->y)}
                         : *entry.position;
        }

        std::vector<std::string> made;
        std::map<std::string, std::string> renamed;
        for (const clip_node& entry : clip.nodes) {
            const std::string was = detail::child_name(entry.node);
            const std::string now = detail::unique_child_name(&g, was);
            runtime::child_node copy = detail::clone_child(entry.node);
            if (copy.module) {
                copy.module->name = now;
            } else {
                copy.group->name = now;
            }
            g.modules.push_back(std::move(copy));
            renamed[was] = now;
            made.push_back(now);

            const std::string full = node_ref{group_path, now}.full();
            if (entry.position) {
                positions_[full] = at && origin ? node_position{at->x + entry.position->x - origin->x,
                                                                at->y + entry.position->y - origin->y}
                                                : *entry.position;
            }
            for (const auto& [rel, p] : entry.subtree_positions) {
                positions_[full + "." + rel] = p;
            }
            for (const auto& [rel, thread] : entry.assignments) {
                const bool declared =
                    std::ranges::any_of(cfg_.threads, [&](const runtime::thread_node& t) { return t.name == thread; });
                if (declared) {
                    cfg_.assignments.emplace_back(rel.empty() ? full : full + "." + rel, thread);
                }
            }
        }

        const auto rewrite = [&](std::string& port_path) {
            const std::string child = detail::port_path_child(port_path);
            const auto it = renamed.find(child);
            if (it != renamed.end()) {
                port_path = it->second + port_path.substr(child.size());
            }
        };
        for (const runtime::connection_node& c : clip.connections) {
            runtime::connection_node fresh = c;
            rewrite(fresh.from);
            rewrite(fresh.to);
            g.connections.push_back(std::move(fresh));
        }
        return made;
    }

    /// Records a connection between two "child.port" paths within a group.
    /// @throws runtime::config_error if the group or a referenced child is missing, a path is
    ///         malformed, an end names a subgroup port that is not exported, or the same connection
    ///         already exists
    void connect(const std::string& group_path, const std::string& from, const std::string& to) {
        runtime::group_node& g = require_group(group_path);
        require_port(g, group_path, from, false);
        require_port(g, group_path, to, true);
        for (const runtime::connection_node& c : g.connections) {
            if (c.from == from && c.to == to) {
                throw runtime::config_error("connection '" + from + "' -> '" + to + "' already exists");
            }
        }
        snapshot();
        g.connections.push_back({from, to});
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

    /// Sets a property value in the project. Editing a live pipeline goes through a separate
    /// channel (session); the project is the source for saving and for the next run.
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
    /// @throws runtime::config_error if the group or the referenced child is missing, the child is
    ///         a subgroup exporting no such input, or a name is malformed
    void set_expose_input(const std::string& group_path, const std::string& alias, const std::string& port_path) {
        runtime::group_node& g = require_group(group_path);
        set_expose(g.expose_inputs, g, group_path, alias, port_path, true);
    }

    /// Exports a child output under a group alias, replacing an alias of the same name.
    /// @throws runtime::config_error if the group or the referenced child is missing, the child is
    ///         a subgroup exporting no such output, or a name is malformed
    void set_expose_output(const std::string& group_path, const std::string& alias, const std::string& port_path) {
        runtime::group_node& g = require_group(group_path);
        set_expose(g.expose_outputs, g, group_path, alias, port_path, false);
    }

    /// Removes an exported input alias, and with it everything above that referenced it.
    /// @throws runtime::config_error if the group or the alias is missing
    void remove_expose_input(const std::string& group_path, const std::string& alias) {
        remove_expose(require_group(group_path).expose_inputs, group_path, alias, true);
    }

    /// Removes an exported output alias. An alias is the only handle the outside has on a port
    /// buried in a group, so losing one invalidates whatever pointed at it: a re-export in the
    /// parent goes too, and so does the alias *that* published, all the way up, together with every
    /// connection naming one of them. Without the cascade those references survive as paths that
    /// resolve to nothing and the project only fails when the pipeline is built.
    /// @throws runtime::config_error if the group or the alias is missing
    void remove_expose_output(const std::string& group_path, const std::string& alias) {
        remove_expose(require_group(group_path).expose_outputs, group_path, alias, false);
    }

    /// Renames an exported input alias, pointing the parent's reference at the new name.
    /// @throws runtime::config_error if the group or the alias is missing, or the new name is
    ///         malformed or already taken in this direction
    void rename_expose_input(const std::string& group_path, const std::string& alias, const std::string& new_alias) {
        rename_expose(require_group(group_path).expose_inputs, group_path, alias, new_alias, true);
    }

    /// Renames an exported output alias. Unlike a remove-then-export pair this keeps the export's
    /// identity, so the re-export one level up and the connections naming it survive — renaming is
    /// not supposed to be destructive.
    /// @throws runtime::config_error if the group or the alias is missing, or the new name is
    ///         malformed or already taken in this direction
    void rename_expose_output(const std::string& group_path, const std::string& alias, const std::string& new_alias) {
        rename_expose(require_group(group_path).expose_outputs, group_path, alias, new_alias, false);
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

    /// Joins several editing operations into one undo step: the snapshot is taken when the
    /// outermost scope opens and the operations inside push none of their own. That is what turns a
    /// composite gesture — deleting a selection of links, exports and nodes at once — into a single
    /// Ctrl+Z for the user instead of one step per touched entity. Nested scopes join the outermost
    /// one, so an operation that opens a scope of its own composes without knowing who called it.
    ///
    /// A throw inside a scope leaves the edit applied in part, exactly as a sequence of separate
    /// operations would; the snapshot taken on entry is what lets the user roll the whole thing back.
    class edit_scope {
       public:
        explicit edit_scope(project& p) : p_(p) {
            if (p_.batch_depth_++ == 0) {
                p_.undo_.push_back(runtime::encode(p_.cfg_));
                p_.redo_.clear();
            }
        }

        ~edit_scope() {
            --p_.batch_depth_;
        }

        edit_scope(const edit_scope&) = delete;
        edit_scope& operator=(const edit_scope&) = delete;

       private:
        project& p_;
    };

    /// Whether the project differs from the last state written to, or read from, disk.
    /// @return false right after create(), open() and save(), and again whenever undo brings the
    ///         model back to that exact state
    [[nodiscard]] bool is_modified() const {
        return runtime::encode(cfg_) != saved_;
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
    project() = default;

    void snapshot() {
        if (batch_depth_ > 0) {
            return;
        }
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

    /// The body of a single child removal, with no history of its own — the caller owns the scope.
    /// The group is looked up again per child because the cascade of the previous one may have
    /// reached into the tree above it.
    void remove_one_child(const std::string& group_path, const std::string& name) {
        runtime::group_node& g = require_group(group_path);
        const std::string prefix = name + ".";
        std::erase_if(g.connections, [&](const runtime::connection_node& c) {
            return c.from.starts_with(prefix) || c.to.starts_with(prefix);
        });
        lost_aliases lost = take_exports_of(g, prefix);
        const std::string full = node_ref{group_path, name}.full();
        std::erase_if(cfg_.assignments,
                      [&](const auto& a) { return a.first == full || a.first.starts_with(full + "."); });
        std::erase_if(g.modules, [&](const runtime::child_node& c) { return detail::child_name(c) == name; });
        std::erase_if(positions_, [&](const auto& p) { return p.first == full || p.first.starts_with(full + "."); });
        (void)cascade_lost_aliases(cfg_.pipeline, group_path, std::move(lost));
    }

    void require_free_name(const runtime::group_node& g, const std::string& name) const {
        for (const runtime::child_node& c : g.modules) {
            if (detail::child_name(c) == name) {
                throw runtime::config_error("duplicate child name '" + name + "'");
            }
        }
    }

    void set_expose(std::vector<std::pair<std::string, std::string>>& map,
                    runtime::group_node& g,
                    const std::string& group_path,
                    const std::string& alias,
                    const std::string& port_path,
                    bool input) {
        detail::check_name(alias, "alias");
        require_port(g, group_path, port_path, input);
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
                       const std::string& alias,
                       bool input) {
        if (std::ranges::none_of(map, [&](const auto& e) { return e.first == alias; })) {
            throw runtime::config_error("no alias '" + alias + "' in group '" + group_path + "'");
        }
        snapshot();
        std::erase_if(map, [&](const auto& e) { return e.first == alias; });
        lost_aliases lost;
        (input ? lost.inputs : lost.outputs).push_back(alias);
        (void)cascade_lost_aliases(cfg_.pipeline, group_path, std::move(lost));
    }

    void rename_expose(std::vector<std::pair<std::string, std::string>>& map,
                       const std::string& group_path,
                       const std::string& alias,
                       const std::string& new_alias,
                       bool input) {
        detail::check_name(new_alias, "alias");
        auto it = std::ranges::find_if(map, [&](const auto& e) { return e.first == alias; });
        if (it == map.end()) {
            throw runtime::config_error("no alias '" + alias + "' in group '" + group_path + "'");
        }
        if (new_alias == alias) {
            return;
        }
        if (std::ranges::any_of(map, [&](const auto& e) { return e.first == new_alias; })) {
            throw runtime::config_error("duplicate alias '" + new_alias + "' in group '" + group_path + "'");
        }
        snapshot();
        it->first = new_alias;
        rewrite_alias_above(cfg_.pipeline, group_path, alias, new_alias, input);
    }

    runtime::config cfg_;
    bool had_includes_ = false;
    std::map<std::string, node_position> positions_;
    std::vector<nlohmann::json> undo_, redo_;
    int batch_depth_ = 0;
    nlohmann::json saved_;
};

}  // namespace atp::studio

#endif
