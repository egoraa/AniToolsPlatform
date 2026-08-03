#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atp/mcp/control_tools.hpp>
#include <atp/mcp/tool_registry.hpp>
#include <atp/module.hpp>
#include <atp/runtime/config_model.hpp>
#include <atp/runtime/pipeline_builder.hpp>
#include <atp/studio/project_from_description.hpp>

#include "support/control_tools_checks.hpp"

namespace {

struct mirror_properties : atp::io::properties {
    atp::io::property<int>& step = make<atp::io::property<int>>("step", 1);
    atp::io::property<std::string>& tag = make<atp::io::property<std::string>>("tag", "none");
};
struct number_outputs : atp::io::outputs {
    atp::io::output<int>& number = make<atp::io::output<int>>("number");
};
struct number_inputs : atp::io::inputs {
    atp::io::input<int>& number = make<atp::io::input<int>>("number");
};

class mirror_source
    : public atp::module<atp::io::ports<atp::io::inputs, number_outputs, mirror_properties>, "mirror_source"> {};
class mirror_sink : public atp::module<atp::io::ports<number_inputs>, "mirror_sink"> {};

constexpr const char* mirror_config = R"({
    "version": "2.0",
    "pipeline": {
        "modules": [
            {"module": "mirror_source", "name": "src", "properties": {"step": 7, "tag": "loud"}},
            {
                "group": "stage",
                "modules": [{"module": "mirror_sink", "name": "inner"}],
                "expose": {"inputs": {"in": "inner.number"}}
            }
        ],
        "connections": [{"from": "src.number", "to": "stage.in"}]
    }
})";

class StudioProjectFromDescription : public ::testing::Test {
   protected:
    void SetUp() override {
        app_.registry.add<mirror_source>();
        app_.registry.add<mirror_sink>();
        const atp::runtime::config cfg = atp::runtime::decode(nlohmann::json::parse(mirror_config));
        atp::runtime::build_pipeline(app_.pipe, app_.runner, cfg, app_.registry);
        app_.runner.start(app_.pipe);
        atp::mcp::register_control_tools(tools_, view_);
    }

    void TearDown() override {
        app_.runner.stop();
    }

    atp::runtime::application app_;
    atp_tests::pipeline_view view_{app_.pipe, app_.runner};
    atp::mcp::tool_registry tools_;
};

TEST_F(StudioProjectFromDescription, RebuildsTheConfigTheRemoteWasBuiltFrom) {
    const nlohmann::json described = atp_tests::call_tool(tools_, "describe_pipeline");
    const atp::studio::project mirror = atp::studio::project_from_description(described);

    const nlohmann::json rebuilt = atp::runtime::encode(mirror.config());
    const nlohmann::json original = atp::runtime::encode(atp::runtime::decode(nlohmann::json::parse(mirror_config)));
    EXPECT_EQ(rebuilt.at("pipeline"), original.at("pipeline"));
}

TEST_F(StudioProjectFromDescription, GivesEveryNodeAPositionSoTheCanvasCanDrawIt) {
    const nlohmann::json described = atp_tests::call_tool(tools_, "describe_pipeline");
    const atp::studio::project mirror = atp::studio::project_from_description(described);

    EXPECT_TRUE(mirror.position("src").has_value());
    EXPECT_TRUE(mirror.position("stage").has_value());
    EXPECT_TRUE(mirror.position("stage.inner").has_value());
}

TEST_F(StudioProjectFromDescription, RefusesADescriptionThatDoesNotFoldIntoAValidConfig) {
    nlohmann::json broken = atp_tests::call_tool(tools_, "describe_pipeline");
    broken.at("connections").at(0).at("to") = "notaportpath";
    EXPECT_THROW((void)atp::studio::project_from_description(broken), atp::runtime::config_error);
}

}  // namespace
