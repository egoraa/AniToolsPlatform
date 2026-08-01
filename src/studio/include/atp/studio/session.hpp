#ifndef ATP_STUDIO_SESSION_HPP
#define ATP_STUDIO_SESSION_HPP

#include <any>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <atp/group.hpp>
#include <atp/pipeline.hpp>
#include <atp/pipeline_runner.hpp>
#include <atp/runtime/config_model.hpp>
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

    /// Monitoring sample of one connection. The (group path, index) pair matches the project's,
    /// since build_group preserves declaration order.
    struct connection_sample {
        std::string group_path;
        std::size_t index = 0;
        std::optional<std::any> value;
        std::uint64_t writes = 0;
    };

    /// Samples every connection of the pipeline for monitoring; empty if nothing has been started.
    [[nodiscard]] std::vector<connection_sample> sample_connections() const {
        std::vector<connection_sample> out;
        if (pipe_) {
            collect(pipe_->root(), "", out);
        }
        return out;
    }

   private:
    void collect(const group& g, const std::string& path, std::vector<connection_sample>& out) const {
        std::size_t index = 0;
        for (const group::connection& c : g.connections()) {
            out.push_back({path, index, c.out->peek(), c.out->write_count()});
            ++index;
        }
        for (const group::child& child : g.children()) {
            if (const auto* sub = dynamic_cast<const group*>(child.module.get())) {
                collect(*sub, path.empty() ? child.name : path + "." + child.name, out);
            }
        }
    }

    module_registry* registry_;
    std::unique_ptr<pipeline> pipe_;
    std::unique_ptr<pipeline_runner> runner_;
};

}  // namespace atp::studio

#endif
