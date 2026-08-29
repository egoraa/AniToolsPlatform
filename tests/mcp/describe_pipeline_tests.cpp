// SPDX-License-Identifier: Apache-2.0
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atp/mcp/control_tools.hpp>
#include <atp/mcp/tool_registry.hpp>
#include <atp/module.hpp>
#include <atp/runtime/group.hpp>
#include <atp/runtime/pipeline.hpp>
#include <atp/runtime/pipeline_runner.hpp>

#include "support/control_tools_checks.hpp"

namespace {

struct mixed_properties : atp::io::properties {
    atp::io::property<int>& count = make<atp::io::property<int>>("count", 1);
    atp::io::property<bool>& loud = make<atp::io::property<bool>>("loud", false);
    atp::io::property<std::string>& tag = make<atp::io::property<std::string>>("tag", "none");
};
struct number_outputs : atp::io::outputs {
    atp::io::output<int>& number = make<atp::io::output<int>>("number");
};
struct number_inputs : atp::io::inputs {
    atp::io::input<int>& number = make<atp::io::input<int>>("number");
};
struct reversed_outputs : atp::io::outputs {
    atp::io::output<int>& zebra = make<atp::io::output<int>>("zebra");
    atp::io::output<int>& mango = make<atp::io::output<int>>("mango");
    atp::io::output<int>& apple = make<atp::io::output<int>>("apple");
};
struct reversed_properties : atp::io::properties {
    atp::io::property<int>& width = make<atp::io::property<int>>("width", 1);
    atp::io::property<int>& height = make<atp::io::property<int>>("height", 2);
    atp::io::property<int>& depth = make<atp::io::property<int>>("depth", 3);
};

class described_source
    : public atp::module<atp::ports<atp::io::inputs, number_outputs, mixed_properties>, "described_source"> {};
class described_sink : public atp::module<atp::ports<number_inputs>, "described_sink"> {};
class reversed_module
    : public atp::module<atp::ports<atp::io::inputs, reversed_outputs, reversed_properties>, "reversed"> {};

class McpDescribePipeline : public ::testing::Test {
   protected:
    void SetUp() override {
        atp::runtime::group& root = pipe_.root();
        root.make<described_source>("src");
        atp::runtime::group& stage = root.add_group("stage");
        stage.make<described_sink>("inner");
        stage.expose_input("in", "inner.number");
        root.connect("src.number", "stage.in");
        runner_.start(pipe_);
        atp::mcp::register_control_tools(tools_, view_);
    }

    void TearDown() override {
        runner_.stop();
    }

    [[nodiscard]] nlohmann::json describe() {
        return atp_tests::call_tool(tools_, "describe_pipeline");
    }

    atp::runtime::pipeline pipe_;
    atp::runtime::pipeline_runner runner_;
    atp_tests::pipeline_view view_{pipe_, runner_};
    atp::mcp::tool_registry tools_;
};

TEST_F(McpDescribePipeline, NamesBothEndsOfEveryConnection) {
    const nlohmann::json described = describe();
    ASSERT_EQ(described.at("connections").size(), 1u);
    EXPECT_EQ(described.at("connections").at(0).at("from"), "src.number");
    EXPECT_EQ(described.at("connections").at(0).at("to"), "stage.in");
}

TEST_F(McpDescribePipeline, ResolvesAGroupAliasToThePortInside) {
    const nlohmann::json described = describe();
    ASSERT_EQ(described.at("modules").size(), 3u);
    const nlohmann::json& stage = described.at("modules").at(1);
    EXPECT_EQ(stage.at("path"), "stage");
    EXPECT_EQ(stage.at("group"), true);
    EXPECT_EQ(stage.at("expose").at("inputs").at("in"), "inner.number");
    EXPECT_TRUE(stage.at("connections").empty());
}

TEST_F(McpDescribePipeline, ReportsThePropertyKindSoAClientCanPickAnEditor) {
    const nlohmann::json properties = describe().at("modules").at(0).at("properties");
    ASSERT_EQ(properties.size(), 3u);
    EXPECT_EQ(properties.at(0).at("name"), "count");
    EXPECT_EQ(properties.at(0).at("kind"), "integer");
    EXPECT_EQ(properties.at(1).at("name"), "loud");
    EXPECT_EQ(properties.at(1).at("kind"), "boolean");
    EXPECT_EQ(properties.at(2).at("name"), "tag");
    EXPECT_EQ(properties.at(2).at("kind"), "text");
}

class McpDeclarationOrder : public ::testing::Test {
   protected:
    void SetUp() override {
        pipe_.root().make<reversed_module>("rev");
        runner_.start(pipe_);
        atp::mcp::register_control_tools(tools_, view_);
    }

    void TearDown() override {
        runner_.stop();
    }

    atp::runtime::pipeline pipe_;
    atp::runtime::pipeline_runner runner_;
    atp_tests::pipeline_view view_{pipe_, runner_};
    atp::mcp::tool_registry tools_;
};

TEST_F(McpDeclarationOrder, PortsAndPropertiesComeOutAsDeclared) {
    const nlohmann::json described = atp_tests::call_tool(tools_, "describe_pipeline");
    ASSERT_EQ(described.at("modules").size(), 1u);
    const nlohmann::json& node = described.at("modules").at(0);

    std::vector<std::string> outputs;
    for (const nlohmann::json& p : node.at("outputs")) {
        outputs.push_back(p.at("name").get<std::string>());
    }
    EXPECT_EQ(outputs, (std::vector<std::string>{"zebra", "mango", "apple"}));

    std::vector<std::string> properties;
    for (const nlohmann::json& p : node.at("properties")) {
        properties.push_back(p.at("name").get<std::string>());
    }
    EXPECT_EQ(properties, (std::vector<std::string>{"width", "height", "depth"}));
}

}  // namespace
