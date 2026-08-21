// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_REMOTE_RUNTIME_HPP
#define ATP_STUDIO_REMOTE_RUNTIME_HPP

#include <any>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <atp/studio/remote_client.hpp>
#include <atp/studio/runtime_view_base.hpp>

namespace atp::studio {

/// A remote pipeline seen as a runtime view: every question is one tool call over the control
/// channel.
///
/// Two things are cached, for opposite reasons. The description is fetched once, because a remote
/// host does not rewire itself under the observer and re-reading it on every tick would throw away
/// the canvas selection; it is re-read on demand and after a property edit, so a value the module
/// normalised comes back as the module holds it, not as it was typed. The status is cached for a
/// fraction of a second, because the panels ask running() dozens of times per repaint and each ask
/// would otherwise be a round trip.
///
/// A consequence of that cache is worth naming, because it is what a reader — or a test — trips over:
/// **running() may go on answering true for up to status_lifetime after the remote host has gone.**
/// Nothing here polls. The snapshot is reused without any I/O, and the client only learns the
/// connection is dead when a call fails, so the answer changes at the first call made after the
/// snapshot expires rather than at the moment the host died. Code that has to observe the change
/// waits for it with a deadline; code that asserts it has already happened is asserting a promise
/// this class does not make.
///
/// A failed call is not thrown at the panels: it disconnects the client and shows up through
/// running() and error_text(). That is the state the window has to reach anyway, and a panel is not
/// the place to decide it. The one exception is set_property, whose refusal belongs to the editor
/// that caused it.
class remote_runtime final : public runtime_view_base {
   public:
    /// @param client connected client; it must outlive the view
    /// @throws remote_error if the first description cannot be read
    explicit remote_runtime(remote_client& client) : client_(&client) {
        described_ = client_->call("describe_pipeline");
    }

    /// Re-reads the structure. Not called by the polling loop — only on attach, after an edit, and
    /// on request.
    void refresh_description() {
        described_ = call("describe_pipeline");
    }

    /// The last description, as describe_pipeline returned it.
    [[nodiscard]] const nlohmann::json& described() const {
        return described_;
    }

    [[nodiscard]] bool running() const override {
        return client_->connected() && status().value("running", false);
    }

    [[nodiscard]] std::string error_text() const override {
        if (!last_error_.empty()) {
            return last_error_;
        }
        return status().value("error", std::string());
    }

    [[nodiscard]] std::vector<runtime::pipeline_runner::thread_stats> stats() const override {
        std::vector<runtime::pipeline_runner::thread_stats> out;
        for (const nlohmann::json& t : status().value("threads", nlohmann::json::array())) {
            out.push_back({t.value("name", std::string()), t.value("passes", std::uint64_t{0}),
                           t.value("busy_passes", std::uint64_t{0})});
        }
        return out;
    }

    [[nodiscard]] std::vector<runtime::connection_sample> sample_connections() const override {
        std::vector<runtime::connection_sample> out;
        for (const nlohmann::json& c : call("read_connections").value("connections", nlohmann::json::array())) {
            runtime::connection_sample sample;
            sample.group_path = c.value("group_path", std::string());
            sample.index = c.value("index", std::size_t{0});
            sample.writes = c.value("writes", std::uint64_t{0});
            out.push_back(std::move(sample));
        }
        return out;
    }

    [[nodiscard]] std::vector<runtime::group::module_stats> module_metrics() const override {
        std::vector<runtime::group::module_stats> out;
        for (const nlohmann::json& m : metrics().value("modules", nlohmann::json::array())) {
            out.push_back({m.value("path", std::string()), m.value("calls", std::uint64_t{0}),
                           m.value("busy_calls", std::uint64_t{0}),
                           std::chrono::nanoseconds(m.value("total_ns", std::int64_t{0})),
                           std::chrono::nanoseconds(m.value("max_ns", std::int64_t{0}))});
        }
        return out;
    }

    [[nodiscard]] std::vector<runtime::group::port_stats> input_metrics() const override {
        std::vector<runtime::group::port_stats> out;
        for (const nlohmann::json& p : call("read_input_metrics").value("ports", nlohmann::json::array())) {
            out.push_back({p.value("path", std::string()),
                           {p.value("received", std::uint64_t{0}), p.value("discarded", std::uint64_t{0}),
                            p.value("pending", std::size_t{0}), p.value("peak_pending", std::size_t{0}),
                            p.value("capacity", std::size_t{0})}});
        }
        return out;
    }

    [[nodiscard]] bool metrics_enabled() const override {
        return metrics().value("enabled", false);
    }

    bool set_metrics_enabled(bool on) override {
        try {
            (void)client_->call("set_module_metrics", {{"enabled", on}});
            return true;
        } catch (const remote_error&) {
            return false;
        }
    }

    [[nodiscard]] std::vector<live_property> live_properties(const std::string& module_path) const override {
        std::vector<live_property> out;
        for (const nlohmann::json& node : described_.value("modules", nlohmann::json::array())) {
            if (node.value("path", std::string()) != module_path) {
                continue;
            }
            for (const nlohmann::json& p : node.value("properties", nlohmann::json::array())) {
                out.push_back({{p.value("name", std::string()), kind_of(p.value("kind", std::string("text"))),
                                p.value("default", std::string()), p.value("options", std::vector<std::string>{}),
                                p.value("persistent", true)},
                               p.value("value", std::string())});
            }
        }
        return out;
    }

    void set_property(const runtime::property_override& o) override {
        (void)client_->call("set_live_property", {{"path", o.module_path + "." + o.name}, {"value", o.value}});
        refresh_description();
    }

   private:
    /// How long a status snapshot is reused. Short enough that a person cannot see it, long enough
    /// that one repaint costs one round trip instead of thirty.
    static constexpr std::chrono::milliseconds status_lifetime{100};

    [[nodiscard]] io::property_kind kind_of(const std::string& name) const {
        if (name == "number") {
            return io::property_kind::number;
        }
        if (name == "boolean") {
            return io::property_kind::boolean;
        }
        return io::property_kind::text;
    }

    [[nodiscard]] const nlohmann::json& status() const {
        const auto now = std::chrono::steady_clock::now();
        if (status_.is_null() || now - status_at_ >= status_lifetime) {
            status_ = call("get_status");
            status_at_ = now;
        }
        return status_;
    }

    [[nodiscard]] const nlohmann::json& metrics() const {
        metrics_ = call("read_module_metrics");
        return metrics_;
    }

    [[nodiscard]] nlohmann::json call(const std::string& tool,
                                      const nlohmann::json& arguments = nlohmann::json::object()) const {
        try {
            nlohmann::json answer = client_->call(tool, arguments);
            last_error_.clear();
            return answer;
        } catch (const remote_error& e) {
            last_error_ = e.what();
            return nlohmann::json::object();
        }
    }

    remote_client* client_;
    nlohmann::json described_;
    mutable nlohmann::json status_;
    mutable nlohmann::json metrics_;
    mutable std::chrono::steady_clock::time_point status_at_;
    mutable std::string last_error_;
};

}  // namespace atp::studio

#endif
