// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_SESSION_HPP
#define ATP_STUDIO_SESSION_HPP

#include <exception>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include <atp/group.hpp>
#include <atp/pipeline.hpp>
#include <atp/pipeline_runner.hpp>
#include <atp/runtime/config_model.hpp>
#include <atp/runtime/connection_sample.hpp>
#include <atp/runtime/pipeline_builder.hpp>
#include <atp/runtime/property_override.hpp>

namespace atp::studio {

/// Execution of a project: every run gets a fresh pipeline and runner, while the module registry
/// lives at session level and survives the runs. The session is owned by the GUI thread, which is
/// therefore the runner's owner too — the owner-thread-only contract holds by construction.
class session {
   public:
    /// @param registry registry the modules are created from; it must outlive the session
    explicit session(module_registry& registry) : registry_(&registry) {}

    /// Builds a pipeline from the config and starts it. On failure the session stays clean and fit
    /// for another run.
    /// @throws std::logic_error if a run is already in progress; runtime::config_error and anything
    ///         the start cascade throws propagate as well
    void start(const runtime::config& cfg) {
        if (running()) {
            throw std::logic_error("session is already running");
        }
        auto pipe = std::make_unique<pipeline>();
        auto runner = std::make_unique<pipeline_runner>();
        runtime::build_pipeline(*pipe, *runner, cfg, *registry_);
        runner->start(*pipe);
        pipe_ = std::move(pipe);
        runner_ = std::move(runner);
    }

    /// Stops the run; a no-op if nothing is running.
    void stop() {
        if (runner_) {
            runner_->stop();
        }
    }

    /// Whether a pipeline is currently running.
    [[nodiscard]] bool running() const {
        return runner_ && runner_->running();
    }

    /// Edits a property of a live module — studio's on-the-fly channel. It does not touch the
    /// project: persistent edits are mirrored into it by the inspector itself.
    /// @throws std::logic_error if nothing is running; runtime::config_error if the path does not
    ///         resolve or the value is rejected
    void set_property(const runtime::property_override& o) {
        if (!running()) {
            throw std::logic_error("session is not running");
        }
        runtime::apply_property_override(pipe_->root(), o);
    }

    /// Live module tree, or nullptr if nothing is running — used by the inspector to read current
    /// values and by sync_persistent_properties when saving on the fly. The check is running()
    /// rather than the presence of a pipeline: a stopped one is still alive until the next start,
    /// but no longer counts as a live tree.
    [[nodiscard]] group* live_root() const {
        return running() ? &pipe_->root() : nullptr;
    }

    /// First execution error; nullptr if nothing ran or the run was clean.
    [[nodiscard]] std::exception_ptr error() const {
        return runner_ ? runner_->error() : nullptr;
    }

    /// Pass counters per thread; empty if nothing has been started yet.
    [[nodiscard]] std::vector<pipeline_runner::thread_stats> stats() const {
        return runner_ ? runner_->stats() : std::vector<pipeline_runner::thread_stats>{};
    }

    /// Turns per-module timing on or off for the whole running pipeline; does nothing when nothing
    /// is running, since the counters live in the tree the runner is driving.
    ///
    /// It is a switch rather than a setting because measuring is not free: timing every iterate cost
    /// a quarter of the throughput of a two-module pipeline (docs/benchmarks.md). Turning it on is
    /// something a person does while asking "which module is slow", not something a deployment
    /// leaves on.
    /// @return false if there is nothing running to enable it on
    bool set_metrics_enabled(bool on) {
        if (group* root = live_root()) {
            root->set_metrics_enabled(on);
            return true;
        }
        return false;
    }

    /// Whether the running pipeline is timing its modules; false when nothing runs.
    [[nodiscard]] bool metrics_enabled() const {
        const group* root = live_root();
        return root != nullptr && root->metrics_enabled();
    }

    /// Per-module cost of the running pipeline; empty if nothing has been started or metrics were
    /// never enabled. The counters accumulate from the moment they are switched on, so a reader
    /// comparing two samples sees the interval between them.
    [[nodiscard]] std::vector<group::module_stats> module_metrics() const {
        const group* root = live_root();
        return root ? root->metrics() : std::vector<group::module_stats>{};
    }

    /// What every input of the running pipeline received and lost; empty if nothing runs. Needs
    /// nothing switched on, unlike the module timing.
    [[nodiscard]] std::vector<group::port_stats> input_metrics() const {
        const group* root = live_root();
        return root ? root->input_metrics() : std::vector<group::port_stats>{};
    }

    /// Monitoring sample of one connection. The type lives in the runtime, since the headless host
    /// samples the same way and only one of the two owns a session.
    using connection_sample = runtime::connection_sample;

    /// Samples every connection of the pipeline for monitoring; empty if nothing has been started.
    [[nodiscard]] std::vector<runtime::connection_sample> sample_connections() const {
        return pipe_ ? runtime::sample_connections(pipe_->root()) : std::vector<runtime::connection_sample>{};
    }

    /// Drains what the modules have said since the previous call.
    ///
    /// It empties what it reads, so exactly one caller may do this — a second one would see a part
    /// of the log and neither would see it whole. Empty before the first run, and after a stop the
    /// buffers of the pipeline that is still alive are drained as usual.
    /// @return the lines, oldest first within one module
    [[nodiscard]] std::vector<log_line> collect_logs() {
        return pipe_ ? pipe_->collect_logs() : std::vector<log_line>{};
    }

   private:
    module_registry* registry_;
    std::unique_ptr<pipeline> pipe_;
    std::unique_ptr<pipeline_runner> runner_;
};

}  // namespace atp::studio

#endif
