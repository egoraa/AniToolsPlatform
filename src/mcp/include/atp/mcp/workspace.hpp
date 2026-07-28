#ifndef ATP_MCP_WORKSPACE_HPP
#define ATP_MCP_WORKSPACE_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <atp/runtime/config_model.hpp>
#include <atp/studio/document.hpp>
#include <atp/studio/module_manager.hpp>
#include <atp/studio/session.hpp>

namespace atp::mcp {

/// Everything one server process edits and runs: the same three objects the GUI owns, plus the path
/// policy. There is exactly one of each — the agent works on one document at a time, the way a
/// studio window does.
class workspace {
   public:
    /// @param root directory every client-supplied path is confined to
    /// @param plugin_dirs extra directories plugins may additionally be loaded from
    /// @param scan_dirs directories scanned for plugins right away and by every later rescan; naming
    ///        one is already a statement of trust, so it widens the plugin path policy as well
    explicit workspace(std::filesystem::path root,
                       std::vector<std::filesystem::path> plugin_dirs = {},
                       std::vector<std::filesystem::path> scan_dirs = {})
        : root_(std::filesystem::weakly_canonical(std::move(root))) {
        for (std::filesystem::path& dir : plugin_dirs) {
            plugin_dirs_.push_back(std::filesystem::weakly_canonical(std::move(dir)));
        }
        for (std::filesystem::path& dir : scan_dirs) {
            std::filesystem::path canonical = std::filesystem::weakly_canonical(std::move(dir));
            plugin_dirs_.push_back(canonical);
            modules_.add_search_dir(std::move(canonical));
        }
        // The catalog is filled here rather than on the first request: an agent's opening move is to
        // list the modules, and an empty answer is indistinguishable from a host that has none.
        // A failing plugin is recorded in its plugin_info, so this cannot throw a startup away.
        modules_.rescan();
    }

    /// Registry and plugin loaders; survives runs, as it does in the studio.
    [[nodiscard]] studio::module_manager& modules() {
        return modules_;
    }

    /// The edited document.
    [[nodiscard]] studio::document& doc() {
        return document_;
    }

    /// The execution of that document.
    [[nodiscard]] studio::session& run_session() {
        return session_;
    }

    /// Directory every client-supplied path is confined to.
    [[nodiscard]] const std::filesystem::path& root() const {
        return root_;
    }

    /// Where the document was last opened from or saved to; nullopt for a document that has never
    /// touched the disk.
    [[nodiscard]] const std::optional<std::filesystem::path>& document_path() const {
        return document_path_;
    }

    /// Directory the config's relative paths — plugins above all — are resolved against. Falls back
    /// to the root while the document has no file of its own.
    [[nodiscard]] std::filesystem::path document_dir() const {
        return document_path_ ? document_path_->parent_path() : root_;
    }

    /// Turns a client-supplied path into an absolute one inside the root.
    /// @param path the path as the client wrote it
    /// @return the resolved absolute path
    /// @throws runtime::config_error if it is empty or the result lies outside the root
    [[nodiscard]] std::filesystem::path resolve(const std::string& path) const {
        const std::filesystem::path candidate = candidate_for(path);
        if (!contains(root_, candidate)) {
            throw runtime::config_error("path '" + path + "' is outside the workspace root '" + root_.string() + "'");
        }
        return candidate;
    }

    /// Turns a client-supplied plugin path into an absolute one. Loading a plugin runs foreign code,
    /// so it is the one operation with its own policy: the root, plus any directory the operator
    /// listed at startup. Everything else stays root-only.
    /// @param path the path as the client wrote it
    /// @return the resolved absolute path
    /// @throws runtime::config_error if it lies outside the root and outside every listed directory
    [[nodiscard]] std::filesystem::path resolve_plugin(const std::string& path) const {
        const std::filesystem::path candidate = candidate_for(path);
        if (contains(root_, candidate)) {
            return candidate;
        }
        for (const std::filesystem::path& dir : plugin_dirs_) {
            if (contains(dir, candidate)) {
                return candidate;
            }
        }
        throw runtime::config_error("plugin '" + path + "' is outside the workspace root and every --plugin-dir");
    }

    /// Throws the document away and starts an empty one.
    void reset_document() {
        document_ = studio::document::create();
        document_path_.reset();
    }

    /// Opens a config together with its layout sidecar.
    /// @throws runtime::config_error on a read or validation failure
    void open_document(const std::filesystem::path& file) {
        document_ = studio::document::open(file);
        document_path_ = file;
    }

    /// Writes the config and its layout sidecar.
    /// @throws runtime::config_error if the config cannot be written
    void save_document(const std::filesystem::path& file) {
        document_.save(file);
        document_path_ = file;
    }

   private:
    /// Absolute form of a client-supplied path. operator/ with an absolute right-hand side yields
    /// that path, so a relative path lands under the root and an absolute one is left alone — the
    /// containment check then covers both cases.
    [[nodiscard]] std::filesystem::path candidate_for(const std::string& path) const {
        if (path.empty()) {
            throw runtime::config_error("path must not be empty");
        }
        return std::filesystem::weakly_canonical(root_ / path);
    }

    /// Whether a resolved path lies inside a directory.
    [[nodiscard]] static bool contains(const std::filesystem::path& dir, const std::filesystem::path& candidate) {
        const std::filesystem::path relative = candidate.lexically_relative(dir);
        return !relative.empty() && *relative.begin() != "..";
    }

    // Declaration order is destruction order reversed: the session holds a reference to the
    // registry, so the manager has to outlive it.
    std::filesystem::path root_;
    std::vector<std::filesystem::path> plugin_dirs_;
    studio::module_manager modules_;
    studio::document document_ = studio::document::create();
    studio::session session_{modules_.registry()};
    std::optional<std::filesystem::path> document_path_;
};

}  // namespace atp::mcp

#endif  // ATP_MCP_WORKSPACE_HPP
