// SPDX-License-Identifier: Apache-2.0
#include <memory>
#include <stop_token>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atp/mcp/application_control.hpp>
#include <atp/mcp/control_tools.hpp>
#include <atp/mcp/tool_registry.hpp>
#include <atp/module.hpp>
#include <atp/runtime/config_model.hpp>
#include <atp/runtime/pipeline_builder.hpp>

#include "support/control_tools_checks.hpp"

namespace {

struct step_properties : atp::io::properties {
    atp::io::property<int>& step = make<atp::io::property<int>>("step", 1);
};
struct number_outputs : atp::io::outputs {
    atp::io::output<int>& number = make<atp::io::output<int>>("number");
};
struct number_inputs : atp::io::inputs {
    atp::io::queued_input<int>& number = make<atp::io::queued_input<int>>("number");
};

class control_source
    : public atp::module<atp::io::ports<atp::io::inputs, number_outputs, step_properties>, "control_source"> {
   public:
    atp::work_status iterate(std::stop_token) override {
        outputs().number(properties().step.get());
        return atp::work_status::busy;
    }
};

class control_sink : public atp::module<atp::io::ports<number_inputs>, "control_sink"> {
   public:
    atp::work_status iterate(std::stop_token) override {
        return inputs().number.try_pop() ? atp::work_status::busy : atp::work_status::idle;
    }
};

class McpApplicationControl : public ::testing::Test {
   protected:
    void SetUp() override {
        app_ = std::make_unique<atp::runtime::application>();
        app_->registry.add<control_source>();
        app_->registry.add<control_sink>();
        const atp::runtime::config cfg = atp::runtime::decode(nlohmann::json::parse(atp_tests::control_target_config));
        atp::runtime::build_pipeline(app_->pipe, app_->runner, cfg, app_->registry);
        app_->runner.start(app_->pipe);
        live_ = std::make_unique<atp::mcp::application_control>(*app_);
        atp::mcp::register_control_tools(tools_, *live_);
    }

    void TearDown() override {
        if (app_) {
            app_->runner.stop();
        }
        live_.reset();
        app_.reset();
    }

    std::unique_ptr<atp::runtime::application> app_;
    std::unique_ptr<atp::mcp::application_control> live_;
    atp::mcp::tool_registry tools_;
};

TEST_F(McpApplicationControl, AnswersEverythingTheSessionDoes) {
    atp_tests::check_control_tools(tools_);
}

TEST_F(McpApplicationControl, TheShutdownToolOnlyAsksAndLeavesTheDecisionToTheHost) {
    int asked = 0;
    atp::mcp::register_shutdown_tool(tools_, [&asked] { ++asked; });

    EXPECT_EQ(atp_tests::call_tool(tools_, "stop").at("stopping"), true);
    EXPECT_EQ(asked, 1);
    EXPECT_EQ(atp_tests::call_tool(tools_, "get_status").at("running"), true);
}

}  // namespace
