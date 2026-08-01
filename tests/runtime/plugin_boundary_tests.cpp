#include <stdexcept>
#include <stop_token>

#include <gtest/gtest.h>

#include <atp/io.hpp>
#include <atp/module.hpp>
#include <atp/module_loader.hpp>
#include <atp/service_directory.hpp>

#include "test_plugin/boundary_types.hpp"

namespace {

struct host_inputs : atp::io::inputs {
    atp::io::input<atp_tests::boundary_payload>& in = make<atp::io::input<atp_tests::boundary_payload>>("in");
};
struct host_outputs : atp::io::outputs {
    atp::io::output<atp_tests::boundary_payload>& out = make<atp::io::output<atp_tests::boundary_payload>>("out");
};
class host_module : public atp::module<atp::io::ports<host_inputs, host_outputs>, "host_echo"> {};

}  // namespace

TEST(PluginBoundary, ConnectsTypedPortsInBothDirections) {
    atp::module_registry registry;
    atp::module_loader loader{ATP_TEST_PLUGIN_PORTS, registry};
    host_module host;
    atp::module_ptr echo = registry.create("plugin_echo");
    ASSERT_NE(echo, nullptr);

    atp::io::output_base& host_out = host.outputs().at("out");
    atp::io::input_base& host_in = host.inputs().at("in");
    atp::io::input_base& plugin_in = echo->inputs().at("in");
    atp::io::output_base& plugin_out = echo->outputs().at("out");

    EXPECT_NO_THROW(host_out.connect(plugin_in));
    EXPECT_NO_THROW(plugin_out.connect(host_in));

    EXPECT_EQ(host_out.connections(), 1u);
    EXPECT_EQ(plugin_out.connections(), 1u);

    host_out.disconnect_all();
    plugin_out.disconnect_all();
}

TEST(PluginBoundary, RefusesAnIncompatibleTypeAcrossTheBoundary) {
    atp::module_registry registry;
    atp::module_loader loader{ATP_TEST_PLUGIN_PORTS, registry};
    host_module host;
    atp::module_ptr echo = registry.create("plugin_echo");
    ASSERT_NE(echo, nullptr);

    atp::io::output_base& host_out = host.outputs().at("out");
    EXPECT_THROW(host_out.connect(echo->inputs().at("number")), std::runtime_error);
    EXPECT_EQ(host_out.connections(), 0u);
}

TEST(PluginBoundary, DeliversAValueThroughThePlugin) {
    atp::module_registry registry;
    atp::module_loader loader{ATP_TEST_PLUGIN_PORTS, registry};
    host_module host;
    atp::module_ptr echo = registry.create("plugin_echo");
    ASSERT_NE(echo, nullptr);
    atp::io::output_base& host_out = host.outputs().at("out");
    atp::io::output_base& plugin_out = echo->outputs().at("out");
    host_out.connect(echo->inputs().at("in"));
    plugin_out.connect(host.inputs().at("in"));

    host.outputs().out(atp_tests::boundary_payload{.value = 7});
    EXPECT_EQ(echo->iterate(std::stop_token{}), atp::work_status::busy);
    EXPECT_EQ(host.inputs().in.get().value, 7);

    host_out.disconnect_all();
    plugin_out.disconnect_all();
}

TEST(PluginBoundary, LooksUpAPluginPortByItsConcreteType) {
    atp::module_registry registry;
    atp::module_loader loader{ATP_TEST_PLUGIN_PORTS, registry};
    atp::module_ptr echo = registry.create("plugin_echo");
    ASSERT_NE(echo, nullptr);

    auto& typed = echo->inputs().get<atp::io::input<atp_tests::boundary_payload>>("in");
    typed(atp_tests::boundary_payload{.value = 5});
    EXPECT_EQ(typed.get().value, 5);

    EXPECT_THROW((void)echo->inputs().get<atp::io::queued_input<atp_tests::boundary_payload>>("in"),
                 std::runtime_error);
}

TEST(PluginBoundary, FindsAServicePublishedByThePlugin) {
    atp::module_registry registry;
    atp::module_loader loader{ATP_TEST_PLUGIN_PORTS, registry};
    atp::service_directory services;
    atp::module_context context{services};
    atp::module_ptr echo = registry.create("plugin_echo");
    ASSERT_NE(echo, nullptr);
    echo->initialize(context);

    atp_tests::boundary_service* service = services.find<atp_tests::boundary_service>("echo");
    ASSERT_NE(service, nullptr);
    EXPECT_EQ(service->doubled(21), 42);
    EXPECT_NO_THROW((void)services.at<atp_tests::boundary_service>("echo"));

    echo->stop();
    EXPECT_EQ(services.find<atp_tests::boundary_service>("echo"), nullptr);
}
