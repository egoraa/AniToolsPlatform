// SPDX-License-Identifier: Apache-2.0
#include <chrono>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atp/mcp/application_control.hpp>
#include <atp/mcp/control_tools.hpp>
#include <atp/mcp/resource_registry.hpp>
#include <atp/mcp/server.hpp>
#include <atp/mcp/socket_server.hpp>
#include <atp/mcp/tool_registry.hpp>
#include <atp/module.hpp>
#include <atp/runtime/config_model.hpp>
#include <atp/runtime/pipeline_builder.hpp>
#include <atp/studio/local_runtime.hpp>
#include <atp/studio/remote_runtime.hpp>

namespace {

using namespace std::chrono_literals;

struct view_properties : atp::io::properties {
    atp::io::property<int>& step = make<atp::io::property<int>>("step", 1);
};
struct number_outputs : atp::io::outputs {
    atp::io::output<int>& number = make<atp::io::output<int>>("number");
};
struct number_inputs : atp::io::inputs {
    atp::io::queued_input<int>& number = make<atp::io::queued_input<int>>("number");
};

class view_source
    : public atp::module<atp::io::ports<atp::io::inputs, number_outputs, view_properties>, "view_source"> {
   public:
    atp::work_status iterate(std::stop_token) override {
        outputs().number(properties().step.get());
        return atp::work_status::busy;
    }
};
class view_sink : public atp::module<atp::io::ports<number_inputs>, "view_sink"> {
   public:
    atp::work_status iterate(std::stop_token) override {
        return inputs().number.try_pop() ? atp::work_status::busy : atp::work_status::idle;
    }
};

constexpr const char* view_config = R"({
    "version": "3.0",
    "pipeline": {
        "modules": [
            {"module": "view_source", "name": "src", "properties": {"step": 2}},
            {"module": "view_sink", "name": "dst"}
        ],
        "connections": [{"from": "src.number", "to": "dst.number"}]
    }
})";

void check_view(atp::studio::runtime_view_base& view) {
    EXPECT_TRUE(view.running());
    EXPECT_TRUE(view.error_text().empty());
    EXPECT_FALSE(view.stats().empty());
    EXPECT_EQ(view.sample_connections().size(), 1u);

    EXPECT_FALSE(view.metrics_enabled());
    EXPECT_TRUE(view.set_metrics_enabled(true));
    EXPECT_TRUE(view.metrics_enabled());
    EXPECT_EQ(view.module_metrics().size(), 2u);
    EXPECT_TRUE(view.set_metrics_enabled(false));

    const std::vector<atp::studio::live_property> properties = view.live_properties("src");
    ASSERT_EQ(properties.size(), 1u);
    EXPECT_EQ(properties[0].info.name, "step");
    EXPECT_EQ(properties[0].info.kind, atp::io::property_kind::number);
    EXPECT_EQ(properties[0].info.default_value, "1");
    EXPECT_EQ(properties[0].value, "2");

    view.set_property({"src", "step", "5"});
    EXPECT_EQ(view.live_properties("src")[0].value, "5");
    EXPECT_THROW(view.set_property({"src", "nope", "1"}), std::exception);

    EXPECT_TRUE(view.live_properties("no_such_module").empty());
}

class StudioRuntimeView : public ::testing::Test {
   protected:
    void SetUp() override {
        app_.registry.add<view_source>();
        app_.registry.add<view_sink>();
        const atp::runtime::config cfg = atp::runtime::decode(nlohmann::json::parse(view_config));
        atp::runtime::build_pipeline(app_.pipe, app_.runner, cfg, app_.registry);
        app_.runner.start(app_.pipe);

        live_ = std::make_unique<atp::mcp::application_control>(app_);
        atp::mcp::register_control_tools(tools_, *live_);
        rpc_ = std::make_unique<atp::mcp::server>(tools_, resources_);
        server_ = std::make_unique<atp::mcp::socket_server>(
            0, [this](const nlohmann::json& message) { return rpc_->handle(message); });
    }

    void TearDown() override {
        server_.reset();
        app_.runner.stop();
    }

    atp::runtime::application app_;
    std::unique_ptr<atp::mcp::application_control> live_;
    atp::mcp::tool_registry tools_;
    atp::mcp::resource_registry resources_;
    std::unique_ptr<atp::mcp::server> rpc_;
    std::unique_ptr<atp::mcp::socket_server> server_;
};

TEST_F(StudioRuntimeView, TheRemoteViewAnswersEveryQuestion) {
    atp::studio::remote_client client("127.0.0.1", server_->port(), 2000ms);
    atp::studio::remote_runtime view(client);
    check_view(view);
}

TEST_F(StudioRuntimeView, TheLocalViewAnswersTheSameQuestions) {
    atp::studio::module_manager manager;
    manager.registry().add<view_source>();
    manager.registry().add<view_sink>();
    atp::studio::session run(manager.registry());
    run.start(atp::runtime::decode(nlohmann::json::parse(view_config)));

    atp::studio::local_runtime view(run);
    check_view(view);
    run.stop();
}

TEST_F(StudioRuntimeView, ADisconnectedRemoteStopsClaimingToRun) {
    atp::studio::remote_client client("127.0.0.1", server_->port(), 300ms);
    atp::studio::remote_runtime view(client);
    ASSERT_TRUE(view.running());

    server_.reset();

    EXPECT_FALSE(view.running());
    EXPECT_FALSE(view.error_text().empty());
}

}  // namespace
