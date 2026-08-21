// SPDX-License-Identifier: Apache-2.0
#include <filesystem>
#include <memory>
#include <stop_token>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atp/config/node.hpp>
#include <atp/mcp/control_tools.hpp>
#include <atp/mcp/tool_registry.hpp>
#include <atp/mcp/workspace.hpp>
#include <atp/module.hpp>
#include <atp/runtime/config_model.hpp>
#include <atp/runtime/json_codec.hpp>

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
    : public atp::module<atp::ports<atp::io::inputs, number_outputs, step_properties>, "control_source"> {
   public:
    atp::work_status iterate(std::stop_token) override {
        outputs().number(properties().step.get());
        return atp::work_status::busy;
    }
};

class control_sink : public atp::module<atp::ports<number_inputs>, "control_sink"> {
   public:
    atp::work_status iterate(std::stop_token) override {
        return inputs().number.try_pop() ? atp::work_status::busy : atp::work_status::idle;
    }
};

class McpControlToolsOverSession : public ::testing::Test {
   protected:
    void SetUp() override {
        root_ = std::filesystem::temp_directory_path() / "atp_mcp_control_session";
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_);
        ws_ = std::make_unique<atp::mcp::workspace>(root_);
        ws_->modules().registry().add<control_source>();
        ws_->modules().registry().add<control_sink>();
        ws_->run_session().start(atp::runtime::decode(atp::runtime::json_parse(atp_tests::control_target_config)));
        atp::mcp::register_control_tools(tools_, ws_->run_session());
    }

    void TearDown() override {
        if (ws_) {
            ws_->run_session().stop();
        }
        ws_.reset();
        std::filesystem::remove_all(root_);
    }

    std::filesystem::path root_;
    std::unique_ptr<atp::mcp::workspace> ws_;
    atp::mcp::tool_registry tools_;
};

TEST_F(McpControlToolsOverSession, AnswersEverythingAboutARunningPipeline) {
    atp_tests::check_control_tools(tools_);
}

TEST_F(McpControlToolsOverSession, DescribesTheLiveTreeWithCurrentPropertyValues) {
    const nlohmann::json described = atp_tests::call_tool(tools_, "describe_pipeline");
    EXPECT_EQ(described.at("running"), true);
    ASSERT_EQ(described.at("modules").size(), 2u);

    const nlohmann::json& src = described.at("modules").at(0);
    EXPECT_EQ(src.at("path"), "src");
    EXPECT_EQ(src.at("module"), "control_source");
    EXPECT_EQ(src.at("group"), false);
    ASSERT_EQ(src.at("outputs").size(), 1u);
    EXPECT_EQ(src.at("outputs").at(0).at("name"), "number");
    ASSERT_EQ(src.at("properties").size(), 1u);
    EXPECT_EQ(src.at("properties").at(0).at("name"), "step");
    EXPECT_EQ(src.at("properties").at(0).at("value"), "1");

    (void)atp_tests::call_tool(tools_, "set_live_property", {{"path", "src.step"}, {"value", 9}});
    EXPECT_EQ(atp_tests::call_tool(tools_, "describe_pipeline").at("modules").at(0).at("properties").at(0).at("value"),
              "9");
}

}  // namespace
