#ifndef ATP_STUDIO_UI_APP_STATE_HPP
#define ATP_STUDIO_UI_APP_STATE_HPP

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <QString>

#include <atp/studio/clipboard.hpp>
#include <atp/studio/module_manager.hpp>
#include <atp/studio/project.hpp>
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
