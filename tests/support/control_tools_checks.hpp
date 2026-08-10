// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_TESTS_SUPPORT_CONTROL_TOOLS_CHECKS_HPP
#define ATP_TESTS_SUPPORT_CONTROL_TOOLS_CHECKS_HPP

#include <chrono>
#include <cstdint>
#include <exception>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atp/group.hpp>
#include <atp/mcp/tool_registry.hpp>
#include <atp/pipeline.hpp>
#include <atp/pipeline_runner.hpp>
#include <atp/runtime/config_model.hpp>
#include <atp/runtime/connection_sample.hpp>
#include <atp/runtime/property_override.hpp>

namespace atp_tests {

/// A control target over a bare pipeline: the shape mcp::application_control has, without a config
/// or a module registry, so a test can build a tree by hand and still ask the tools about it.
class pipeline_view {
   public:
    pipeline_view(atp::pipeline& pipe, atp::pipeline_runner& runner) : pipe_(&pipe), runner_(&runner) {}

    [[nodiscard]] bool running() const {
        return runner_->running();
    }

    [[nodiscard]] std::exception_ptr error() const {
        return runner_->error();
    }

    [[nodiscard]] std::vector<atp::pipeline_runner::thread_stats> stats() const {
        return runner_->stats();
    }

    [[nodiscard]] atp::group* live_root() const {
        return running() ? &pipe_->root() : nullptr;
    }

    [[nodiscard]] std::vector<atp::runtime::connection_sample> sample_connections() const {
        return atp::runtime::sample_connections(pipe_->root());
    }

    [[nodiscard]] std::vector<atp::group::module_stats> module_metrics() const {
        const atp::group* root = live_root();
        return root ? root->metrics() : std::vector<atp::group::module_stats>{};
    }

    [[nodiscard]] std::vector<atp::group::port_stats> input_metrics() const {
        const atp::group* root = live_root();
        return root ? root->input_metrics() : std::vector<atp::group::port_stats>{};
    }

    [[nodiscard]] bool metrics_enabled() const {
        const atp::group* root = live_root();
        return root != nullptr && root->metrics_enabled();
    }

    bool set_metrics_enabled(bool on) {
        if (atp::group* root = live_root()) {
            root->set_metrics_enabled(on);
            return true;
        }
        return false;
    }

    void set_property(const atp::runtime::property_override& o) {
        atp::runtime::apply_property_override(pipe_->root(), o);
    }

   private:
    atp::pipeline* pipe_;
    atp::pipeline_runner* runner_;
};

/// The config both control targets are built from: two modules on the implicit main thread, one
/// connection, and a property to edit. Written once, because the point of the checks below is that
/// the two hosts answer identically about the same pipeline.
inline constexpr const char* control_target_config = R"({
    "version": "3.0",
    "pipeline": {
        "modules": [
            {"module": "control_source", "name": "src"},
            {"module": "control_sink", "name": "dst"}
        ],
        "connections": [{"from": "src.number", "to": "dst.number"}]
    }
})";

/// Calls a tool by name, failing the test if it is not registered.
[[nodiscard]] inline nlohmann::json call_tool(const atp::mcp::tool_registry& tools,
                                              const char* name,
                                              nlohmann::json args = nlohmann::json::object()) {
    const atp::mcp::tool* t = tools.find(name);
    EXPECT_NE(t, nullptr) << name;
    return t == nullptr ? nlohmann::json::object() : t->run(args);
}

/// Everything the control tools must answer over a pipeline built from control_target_config. Both
/// targets run it: that a session and a bare runtime::application give the same answers is the whole
/// claim of the shared registration.
///
/// Every answer walked below is held in a named local first. Iterating call_tool(...).at(...)
/// directly reads a subobject of a temporary that dies with the init-expression: C++23 extends it
/// (P2718R0), but a compiler without that paper — gcc 13 among them — leaves the range dangling, and
/// the loop then silently sees nothing rather than crashing.
inline void check_control_tools(const atp::mcp::tool_registry& tools) {
    EXPECT_EQ(call_tool(tools, "get_status").at("running"), true);
    EXPECT_FALSE(call_tool(tools, "get_status").at("threads").empty());

    nlohmann::json seen;
    for (int i = 0; i < 500 && seen.is_null(); ++i) {
        const nlohmann::json sampled = call_tool(tools, "read_connections");
        for (const nlohmann::json& c : sampled.at("connections")) {
            if (c.at("writes").get<std::uint64_t>() > 0) {
                seen = c;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    ASSERT_FALSE(seen.is_null());
    EXPECT_EQ(seen.at("group_path"), "");
    EXPECT_EQ(seen.at("index"), 0);

    EXPECT_EQ(call_tool(tools, "read_module_metrics").at("enabled"), false);
    EXPECT_EQ(call_tool(tools, "set_module_metrics", {{"enabled", true}}).at("enabled"), true);
    EXPECT_EQ(call_tool(tools, "read_module_metrics").at("enabled"), true);
    EXPECT_EQ(call_tool(tools, "read_module_metrics").at("modules").size(), 2u);
    EXPECT_EQ(call_tool(tools, "set_module_metrics", {{"enabled", false}}).at("enabled"), false);

    const nlohmann::json ports = call_tool(tools, "read_input_metrics").at("ports");
    ASSERT_EQ(ports.size(), 1u);
    EXPECT_EQ(ports.at(0).at("path"), "dst.number");
    EXPECT_EQ(ports.at(0).at("capacity"), 32u);
    EXPECT_GT(ports.at(0).at("received").get<std::uint64_t>(), 0u);
    EXPECT_TRUE(ports.at(0).contains("discarded"));
    EXPECT_TRUE(ports.at(0).contains("pending"));
    EXPECT_TRUE(ports.at(0).contains("peak_pending"));

    EXPECT_EQ(call_tool(tools, "set_live_property", {{"path", "src.step"}, {"value", 3}}).at("set"), "src.step");
    EXPECT_THROW((void)call_tool(tools, "set_live_property", {{"path", "src.nope"}, {"value", 1}}),
                 atp::runtime::config_error);
}

}  // namespace atp_tests

#endif
