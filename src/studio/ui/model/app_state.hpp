// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_UI_APP_STATE_HPP
#define ATP_STUDIO_UI_APP_STATE_HPP

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <QString>

#include <atp/studio/clipboard.hpp>
#include <atp/studio/local_runtime.hpp>
#include <atp/studio/module_manager.hpp>
#include <atp/studio/project.hpp>
#include <atp/studio/project_from_description.hpp>
#include <atp/studio/remote_client.hpp>
#include <atp/studio/remote_runtime.hpp>
#include <atp/studio/session.hpp>
#include <atp/studio/settings.hpp>
#include <atp/version.hpp>

namespace atp::studio::ui {

/// The whole application state in one aggregate; the widgets are thin wrappers over it. Member
/// order is destruction order reversed: the session dies before the manager, since its pipeline
/// holds modules from the manager's DLLs.
struct app_state {
    studio_settings settings;
    std::filesystem::path settings_file = default_settings_path();
    module_manager manager;
    project doc = project::create();
    std::optional<std::filesystem::path> doc_path;
    session run{manager.registry()};

    /// The local execution seen through the interface the panels read. Declared after the session it
    /// wraps, so it dies first.
    local_runtime local_view{run};

    /// What the panels read. It points at local_view unless the window is mirroring a remote host.
    runtime_view_base* view = &local_view;

    std::string current_group;
    std::string selected_child;

    /// Detached copies of the last cut or copied selection. It is a snapshot rather than a list of
    /// paths, so cut can remove the source at once and the buffer survives undo and File > Open.
    studio::clipboard clip;

    /// Settings of the last module whose properties were copied. Kept apart from `clip` because the
    /// two are pasted by different gestures onto different things, and one must not silently
    /// clobber the other.
    studio::property_clip clip_properties;

    std::unordered_map<std::string, module_info> describe_cache;

    std::size_t describe_generation = 0;

    /// Drops the cached descriptions; the next request probes the factories again.
    void invalidate_descriptions() {
        describe_cache.clear();
        ++describe_generation;
    }

    /// Directory the project lives in, or the current directory if it has not been saved.
    [[nodiscard]] std::filesystem::path config_dir() const {
        return doc_path ? doc_path->parent_path() : std::filesystem::current_path();
    }

    /// Module description from the cache, computing it on the first request.
    /// @return nullptr if the factory is not registered
    [[nodiscard]] const module_info* describe_cached(const std::string& factory, const std::optional<version>& ver) {
        const std::string key = factory + "@" + (ver ? ver->to_string() : "latest");
        auto it = describe_cache.find(key);
        if (it != describe_cache.end()) {
            return &it->second;
        }
        const module_factory_base* f = ver ? manager.registry().find(factory, *ver) : manager.registry().find(factory);
        if (f == nullptr) {
            return nullptr;
        }
        return &describe_cache.emplace(key, module_manager::describe(*f)).first->second;
    }

    /// Returns the view to the root group with nothing selected.
    void reset_view() {
        current_group.clear();
        selected_child.clear();
    }

    /// The project put aside while the window mirrors a remote host.
    struct stashed_project {
        project doc;
        std::optional<std::filesystem::path> doc_path;
        std::string current_group;
        std::string selected_child;
    };

    /// Connects to a host and replaces the project with a mirror of its pipeline.
    ///
    /// The project is put aside rather than closed: detach() brings it back as it was, the way a
    /// debugger returns you to the file you were editing. Nothing is stashed until both the
    /// connection and the mirror have succeeded, so a failed attach leaves the window where it was.
    /// @param host host name or IPv4 literal
    /// @param port TCP port of the host's control channel
    /// @throws remote_error if the host cannot be reached; runtime::config_error if its pipeline
    ///         does not fold back into a valid config
    void attach(const std::string& host, std::uint16_t port) {
        auto client = std::make_unique<remote_client>(host, port, std::chrono::milliseconds(2000));
        auto remote = std::make_unique<remote_runtime>(*client);
        project mirror = project_from_description(remote->described());

        stashed = stashed_project{std::move(doc), doc_path, current_group, selected_child};
        doc = std::move(mirror);
        doc_path.reset();
        reset_view();
        remote_client_ = std::move(client);
        remote_view_ = std::move(remote);
        remote_client_->set_timeout(std::chrono::milliseconds(500));
        view = remote_view_.get();
    }

    /// Returns to the local project; a no-op when not attached.
    void detach() {
        if (!stashed) {
            return;
        }
        view = &local_view;
        remote_view_.reset();
        remote_client_.reset();
        doc = std::move(stashed->doc);
        doc_path = stashed->doc_path;
        current_group = stashed->current_group;
        selected_child = stashed->selected_child;
        stashed.reset();
    }

    /// Whether the window is mirroring a remote host.
    [[nodiscard]] bool attached() const {
        return remote_view_ != nullptr;
    }

    /// Endpoint being mirrored, for the title bar and the log; empty when local.
    [[nodiscard]] std::string endpoint() const {
        return remote_client_ ? remote_client_->endpoint() : std::string();
    }

    /// Asks the remote host to shut down. Deliberately not reachable from anything the Stop button
    /// touches: the word means a different thing here, since it ends someone else's process.
    /// @throws remote_error if the call does not get through
    void stop_remote() {
        if (remote_client_) {
            (void)remote_client_->call("stop");
        }
    }

    /// Re-reads the remote structure and rebuilds the mirror; a no-op when local.
    /// @throws remote_error or runtime::config_error, leaving the current mirror in place
    void refresh_mirror() {
        if (!attached()) {
            return;
        }
        remote_view_->refresh_description();
        doc = project_from_description(remote_view_->described());
        reset_view();
    }

    /// The client, declared before the view that borrows it, so the view dies first.
    std::unique_ptr<remote_client> remote_client_;
    std::unique_ptr<remote_runtime> remote_view_;
    std::optional<stashed_project> stashed;
};

/// Callbacks instead of signals, which lets the widgets do without Q_OBJECT and moc:
/// project_changed rebuilds the dependent widgets, error writes into the log, and
/// selection_changed refreshes the inspector.
struct ui_callbacks {
    std::function<void()> project_changed;
    std::function<void(const QString&)> error;
    std::function<void()> selection_changed;
};

}  // namespace atp::studio::ui

#endif
