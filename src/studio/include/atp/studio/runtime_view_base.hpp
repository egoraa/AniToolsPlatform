// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_RUNTIME_VIEW_BASE_HPP
#define ATP_STUDIO_RUNTIME_VIEW_BASE_HPP

#include <string>
#include <vector>

#include <atp/runtime/connection_sample.hpp>
#include <atp/runtime/group.hpp>
#include <atp/runtime/pipeline_runner.hpp>
#include <atp/runtime/property_override.hpp>
#include <atp/studio/module_manager.hpp>

namespace atp::studio {

/// One property of a live module: everything the inspector needs to draw a row, plus the value the
/// module holds right now.
struct live_property {
    property_info info;
    std::string value;
};

/// What the runtime panels are allowed to ask, whoever owns the pipeline.
///
/// There is deliberately no live_root() here. A group* means nothing across a socket, and it is
/// exactly what the inspector used to reach through; asking a question — what are this module's
/// properties — instead of handing out the tree is what lets a remote host answer at all.
/// error_text() is a string for the same reason: a remote failure arrives as text, and rebuilding an
/// exception from it would be an invention.
///
/// Virtuals rather than a concept, unlike the server-side control_target: here the two
/// implementations really are chosen at run time, and every call happens a few times a second.
class runtime_view_base {
   public:
    virtual ~runtime_view_base() = default;

    /// Whether a pipeline is running and reachable.
    [[nodiscard]] virtual bool running() const = 0;

    /// First execution error, or an empty string if the run is clean.
    [[nodiscard]] virtual std::string error_text() const = 0;

    /// Pass counters per thread.
    [[nodiscard]] virtual std::vector<runtime::pipeline_runner::thread_stats> stats() const = 0;

    /// Every connection with the number of writes that have travelled it.
    [[nodiscard]] virtual std::vector<runtime::connection_sample> sample_connections() const = 0;

    /// Per-module cost; empty when nothing runs or metrics were never enabled.
    [[nodiscard]] virtual std::vector<runtime::group::module_stats> module_metrics() const = 0;

    /// What every input received and lost; empty when nothing runs. Needs nothing enabled, which is
    /// why it is not paired with metrics_enabled() the way module_metrics() is.
    [[nodiscard]] virtual std::vector<runtime::group::port_stats> input_metrics() const = 0;

    /// Whether the pipeline is timing its modules.
    [[nodiscard]] virtual bool metrics_enabled() const = 0;

    /// Turns per-module timing on or off.
    /// @return false if there is nothing running to enable it on
    virtual bool set_metrics_enabled(bool on) = 0;

    /// Properties of one live module with the values it holds now, sorted by name.
    /// @param module_path dotted path of the module
    /// @return an empty vector if there is no such module or nothing is running
    [[nodiscard]] virtual std::vector<live_property> live_properties(const std::string& module_path) const = 0;

    /// Edits a property of a live module.
    /// @throws runtime::config_error or remote_error if the path does not resolve or the value is
    ///         rejected
    virtual void set_property(const runtime::property_override& o) = 0;
};

}  // namespace atp::studio

#endif
