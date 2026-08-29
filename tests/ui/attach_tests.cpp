// SPDX-License-Identifier: Apache-2.0
#include <cstddef>
#include <memory>
#include <stop_token>
#include <string>

#include <gtest/gtest.h>

#include <QGraphicsScene>
#include <QGraphicsView>

#include <nlohmann/json.hpp>

#include <atp/config/node.hpp>
#include <atp/mcp/application_control.hpp>
#include <atp/mcp/control_tools.hpp>
#include <atp/mcp/resource_registry.hpp>
#include <atp/mcp/server.hpp>
#include <atp/mcp/socket_server.hpp>
#include <atp/mcp/tool_registry.hpp>
#include <atp/module.hpp>
#include <atp/runtime/config_model.hpp>
#include <atp/runtime/json_codec.hpp>
#include <atp/runtime/pipeline_builder.hpp>

#include "model/app_state.hpp"
#include "shell/main_window.hpp"
#include "ui/qt_app.hpp"

namespace {

using atp::studio::ui::app_state;

struct attach_properties : atp::io::properties {
    atp::io::property<int>& step = make<atp::io::property<int>>("step", 1);
};
struct number_outputs : atp::io::outputs {
    atp::io::output<int>& number = make<atp::io::output<int>>("number");
};
struct number_inputs : atp::io::inputs {
    atp::io::queued_input<int>& number = make<atp::io::queued_input<int>>("number");
};

class attach_source
    : public atp::module<atp::ports<atp::io::inputs, number_outputs, attach_properties>, "attach_source"> {
   public:
    atp::work_status iterate(std::stop_token) override {
        outputs().number(properties().step.get());
        return atp::work_status::busy;
    }
};
class attach_sink : public atp::module<atp::ports<number_inputs>, "attach_sink"> {
   public:
    atp::work_status iterate(std::stop_token) override {
        return inputs().number.try_pop() ? atp::work_status::busy : atp::work_status::idle;
    }
};

constexpr const char* attach_config = R"({
    "version": "1.0",
    "pipeline": {
        "modules": [
            {"module": "attach_source", "name": "src", "properties": {"step": 4}},
            {"module": "attach_sink", "name": "dst"}
        ],
        "connections": [{"from": "src.number", "to": "dst.number"}]
    }
})";

class UiAttach : public ::testing::Test {
   protected:
    void SetUp() override {
        (void)atp_ui_tests::ensure_app();
        app_.registry.add<attach_source>();
        app_.registry.add<attach_sink>();
        const atp::runtime::config cfg = atp::runtime::decode(atp::runtime::json_parse(attach_config));
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

    [[nodiscard]] std::uint16_t port() const {
        return server_->port();
    }

    atp::runtime::application app_;
    std::unique_ptr<atp::mcp::application_control> live_;
    atp::mcp::tool_registry tools_;
    atp::mcp::resource_registry resources_;
    std::unique_ptr<atp::mcp::server> rpc_;
    std::unique_ptr<atp::mcp::socket_server> server_;
};

TEST_F(UiAttach, MirrorsTheRemoteGraphAsAProject) {
    app_state state;
    state.attach("127.0.0.1", port());

    EXPECT_TRUE(state.attached());
    EXPECT_TRUE(state.view->running());
    EXPECT_EQ(state.endpoint(), "127.0.0.1:" + std::to_string(port()));
    ASSERT_EQ(state.doc.config().pipeline.modules.size(), 2u);
    EXPECT_EQ(state.doc.config().pipeline.connections.size(), 1u);
    EXPECT_TRUE(state.doc.position("src").has_value());
}

TEST_F(UiAttach, EditsAPropertyOfTheRemoteModule) {
    app_state state;
    state.attach("127.0.0.1", port());

    state.view->set_property({"src", "step", "6"});
    EXPECT_EQ(state.view->live_properties("src")[0].value, "6");
    EXPECT_THROW(state.view->set_property({"src", "nope", "1"}), std::exception);
}

TEST_F(UiAttach, DetachRestoresTheProjectThatWasPutAside) {
    app_state state;
    const std::size_t before = state.doc.config().pipeline.modules.size();

    state.attach("127.0.0.1", port());
    ASSERT_TRUE(state.attached());
    state.detach();

    EXPECT_FALSE(state.attached());
    EXPECT_TRUE(state.endpoint().empty());
    EXPECT_EQ(state.doc.config().pipeline.modules.size(), before);
    EXPECT_EQ(state.view, &state.local_view);
}

TEST_F(UiAttach, ADeadRemoteStopsClaimingToRun) {
    app_state state;
    state.attach("127.0.0.1", port());
    ASSERT_TRUE(state.view->running());

    server_.reset();

    EXPECT_FALSE(state.view->running());
    EXPECT_FALSE(state.view->error_text().empty());
}

TEST_F(UiAttach, TheWindowSaysWhereItIsAttachedAndOffersTheHostActions) {
    app_state state;
    auto window = std::make_unique<atp::studio::ui::main_window>(state);
    EXPECT_FALSE(window->windowTitle().contains("attached"));

    state.attach("127.0.0.1", port());
    window->refresh_all();

    EXPECT_TRUE(window->windowTitle().contains(QString::fromStdString("attached to " + state.endpoint())));
    QGraphicsView* canvas = window->findChild<QGraphicsView*>();
    ASSERT_NE(canvas, nullptr);
    ASSERT_NE(canvas->scene(), nullptr);
    EXPECT_FALSE(canvas->scene()->items().isEmpty());

    state.detach();
    window->refresh_all();
    EXPECT_FALSE(window->windowTitle().contains("attached"));

    window.reset();
}

TEST_F(UiAttach, RefusesAnEndpointNobodyIsListeningOnAndKeepsTheProject) {
    app_state state;
    EXPECT_THROW(state.attach("127.0.0.1", 1), atp::studio::remote_error);
    EXPECT_FALSE(state.attached());
    EXPECT_EQ(state.view, &state.local_view);
}

}  // namespace
