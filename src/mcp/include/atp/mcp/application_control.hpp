// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_MCP_APPLICATION_CONTROL_HPP
#define ATP_MCP_APPLICATION_CONTROL_HPP

#include <exception>
#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <atp/group.hpp>
#include <atp/mcp/arguments.hpp>
#include <atp/mcp/tool_registry.hpp>
#include <atp/pipeline_runner.hpp>
#include <atp/runtime/connection_sample.hpp>
#include <atp/runtime/pipeline_builder.hpp>
#include <atp/runtime/property_override.hpp>

namespace atp::mcp {

/// A built runtime::application seen the way the control tools want to see it — the shape
/// studio::session has, over the objects a headless host already owns. This is the entire cost of
/// reusing the tools: the host keeps building its pipeline exactly as before, and its main stays
/// thin.
class application_control {
   public:
    /// @param app assembled application; it must outlive this object
    explicit application_control(runtime::application& app) : app_(&app) {}

    /// Whether the runner is driving the pipeline.
    [[nodiscard]] bool running() const {
        return app_->runner.running();
    }

    /// First execution error; nullptr if nothing ran or the run is clean.
    [[nodiscard]] std::exception_ptr error() const {
        return app_->runner.error();
    }

    /// Pass counters per thread.
    [[nodiscard]] std::vector<pipeline_runner::thread_stats> stats() const {
        return app_->runner.stats();
    }

    /// Live module tree, or nullptr when nothing runs. The check is running() rather than the
    /// presence of a pipeline, matching session: a stopped tree is still there but no longer live.
    [[nodiscard]] group* live_root() const {
        return running() ? &app_->pipe.root() : nullptr;
    }

    /// Every connection with the last value that travelled it.
    [[nodiscard]] std::vector<runtime::connection_sample> sample_connections() const {
        return runtime::sample_connections(app_->pipe.root());
    }

    /// Per-module cost; empty when nothing runs or metrics were never enabled.
    [[nodiscard]] std::vector<group::module_stats> module_metrics() const {
        const group* root = live_root();
        return root ? root->metrics() : std::vector<group::module_stats>{};
    }

    /// What every input of the running pipeline received and lost; empty if nothing runs.
    [[nodiscard]] std::vector<group::port_stats> input_metrics() const {
        const group* root = live_root();
        return root ? root->input_metrics() : std::vector<group::port_stats>{};
    }

    /// Whether the running pipeline is timing its modules.
    [[nodiscard]] bool metrics_enabled() const {
        const group* root = live_root();
        return root != nullptr && root->metrics_enabled();
    }

    /// Turns per-module timing on or off for the whole running pipeline.
    /// @return false if there is nothing running to enable it on
    bool set_metrics_enabled(bool on) {
        if (group* root = live_root()) {
            root->set_metrics_enabled(on);
            return true;
        }
        return false;
    }

    /// Edits a property of a live module.
    /// @throws std::logic_error if nothing is running; runtime::config_error if the path does not
    ///         resolve or the value is rejected
    void set_property(const runtime::property_override& o) {
        if (!running()) {
            throw std::logic_error("nothing is running");
        }
        runtime::apply_property_override(app_->pipe.root(), o);
    }

   private:
    runtime::application* app_;
};

/// Registers the host's own stop. In atp_mcp the word ends a run and the server lives on; in a
/// headless host it ends the process. Same word, different reach, so it is deliberately not part of
/// the shared set: the tool only asks, and what asking means is the host's to decide — which is also
/// what lets a test exercise it without killing itself.
/// @param tools registry the tool is added to
/// @param request_exit called on the owner thread; it must return promptly, since the reply is
///        written after it
inline void register_shutdown_tool(tool_registry& tools, std::function<void()> request_exit) {
    tools.add({"stop",
               "Stops this host: the pipeline is shut down and the process exits the way it would on "
               "Ctrl+C. There is no way to start it again over this channel.",
               no_arguments_schema(), [request_exit = std::move(request_exit)](const nlohmann::json&) {
                   request_exit();
                   return nlohmann::json{{"stopping", true}};
               }});
}

}  // namespace atp::mcp

#endif
