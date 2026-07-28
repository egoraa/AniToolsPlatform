#include <memory>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <atp/group.hpp>
#include <atp/module.hpp>

#include "pipeline_test_support.hpp"

namespace {

using atp_tests::event_log;
using atp_tests::probe_module;

class named_module : public atp::module<atp::io::ports<>, "named"> {};
class plain_module : public atp::module<> {};

struct number_inputs : atp::io::inputs {
    atp::io::input<int>& number = make<atp::io::input<int>>("number");
};
struct number_outputs : atp::io::outputs {
    atp::io::output<int>& number = make<atp::io::output<int>>("number");
};
using source_ports = atp::io::ports<atp::io::inputs, number_outputs>;
using sink_ports = atp::io::ports<number_inputs>;
class source_module : public atp::module<source_ports> {};
class sink_module : public atp::module<sink_ports> {};

// Tree root[a, stage[b, deep[c]], d] — the shared fixture for the cascades.
struct rig {
    atp::group root{"root"};
    event_log log;
    probe_module* a;
    probe_module* b;
    probe_module* c;
    probe_module* d;
    atp::group* stage;
    atp::group* deep;
    atp::service_directory services;
    atp::module_context ctx{services};

    rig() {
        a = &root.make<probe_module>("a", log, "a");
        stage = &root.add_group("stage");
        b = &stage->make<probe_module>("b", log, "b");
        deep = &stage->add_group("deep");
        c = &deep->make<probe_module>("c", log, "c");
        d = &root.make<probe_module>("d", log, "d");
    }
};

TEST(Group, OwnsChildrenInInsertionOrder) {
    atp::group g("root");
    g.make<named_module>();  // the name comes from the type
    atp::group& sub = g.add_group("sub");
    g.make<plain_module>("tail");  // the name is given at the registration point

    ASSERT_EQ(g.children().size(), 3u);
    EXPECT_EQ(g.children()[0].name, "named");
    EXPECT_EQ(g.children()[1].module.get(), &sub);  // a subgroup is just another child module
    EXPECT_EQ(g.children()[2].name, "tail");
    EXPECT_EQ(g.find_group("sub"), &sub);
    EXPECT_EQ(g.find_group("tail"), nullptr);  // this is a module, not a group
    EXPECT_EQ(sub.get_name(), "sub");
}

TEST(Group, AddAcceptsPrebuiltModule) {
    atp::group g("root");
    atp::module_base& m = g.add("ready", atp::module_ptr{new plain_module});  // e.g. one from module_registry::create()
    EXPECT_EQ(g.find_module("ready"), &m);
    EXPECT_EQ(g.find_module("missing"), nullptr);
}

TEST(Group, RejectsDuplicateAndEmptyNames) {
    atp::group g("root");
    g.make<plain_module>("one");
    EXPECT_THROW(g.make<plain_module>("one"), std::runtime_error);
    EXPECT_THROW(g.add_group("one"), std::runtime_error);  // one shared name space
    EXPECT_THROW(g.make<plain_module>(""), std::invalid_argument);
    EXPECT_THROW(g.add("null", atp::module_ptr{}), std::invalid_argument);
}

TEST(Group, CascadesFollowInsertionOrderAndReverseOnStop) {
    rig r;
    r.root.initialize(r.ctx);
    r.root.start();
    (void)r.root.iterate(std::stop_token{});
    r.root.stop();

    std::vector<std::string> expected{"a", "b", "c", "d"};
    EXPECT_EQ(r.log.order_of("initialize"), expected);
    EXPECT_EQ(r.log.order_of("start"), expected);
    EXPECT_EQ(r.log.order_of("iterate"), expected);
    std::vector<std::string> reversed{"d", "c", "b", "a"};
    EXPECT_EQ(r.log.order_of("stop"), reversed);
}

TEST(Group, InitializeFailureRollsBackLocally) {
    rig r;
    r.c->throw_in = "initialize";
    EXPECT_THROW(r.root.initialize(r.ctx), std::runtime_error);
    // stop reaches those that passed initialize (a, b), in reverse order; d was never touched
    std::vector<std::string> rolled{"b", "a"};
    EXPECT_EQ(r.log.order_of("stop"), rolled);
}

TEST(Group, StopContinuesAfterErrorAndRethrowsFirst) {
    rig r;
    r.root.initialize(r.ctx);
    r.b->throw_in = "stop";
    EXPECT_THROW(r.root.stop(), std::runtime_error);
    // despite b throwing, everyone got stop, and the reverse order held
    std::vector<std::string> reversed{"d", "c", "b", "a"};
    EXPECT_EQ(r.log.order_of("stop"), reversed);
}

TEST(Group, IterateSkipsDetachedAndAggregatesStatus) {
    rig r;
    r.root.set_detached(*r.stage, true);
    EXPECT_EQ(r.root.iterate(std::stop_token{}), atp::work_status::busy);  // the probes report busy
    std::vector<std::string> without_stage{"a", "d"};
    EXPECT_EQ(r.log.order_of("iterate"), without_stage);

    atp::group idle_group("idle");
    idle_group.make<plain_module>("silent");  // the default iterate reports idle
    EXPECT_EQ(idle_group.iterate(std::stop_token{}), atp::work_status::idle);
}

TEST(Group, SetDetachedUnknownChildThrows) {
    atp::group g("root");
    atp::group stranger("stranger");
    EXPECT_THROW(g.set_detached(stranger, true), std::invalid_argument);
}

TEST(Group, ExposesChildPortsAsOwnAliases) {
    atp::group g("root");
    sink_module& sink = g.make<sink_module>("sink");
    source_module& src = g.make<source_module>("src");
    g.expose_input("in", "sink.number");
    g.expose_output("out", "src.number");

    EXPECT_EQ(&g.inputs().at("in"), &sink.inputs().number);  // the same object
    EXPECT_EQ(&g.outputs().at("out"), &src.outputs().number);
    EXPECT_TRUE(g.inputs().owned().empty());  // a group's registries hold aliases only
}

TEST(Group, PortsVisibleThroughModuleBase) {
    atp::group g("stage");
    g.make<sink_module>("sink");
    g.expose_input("in", "sink.number");
    atp::module_base& as_module = g;
    // Composite: from outside a group looks like an ordinary module with ports.
    EXPECT_NE(as_module.inputs().find("in"), nullptr);
}

TEST(Group, ReexportResolvesToRealPortImmediately) {
    atp::group root("root");
    atp::group& inner = root.add_group("inner");
    sink_module& sink = inner.make<sink_module>("sink");
    inner.expose_input("in", "sink.number");
    root.expose_input("outer_in", "inner.in");  // re-exporting an alias

    EXPECT_EQ(&root.inputs().at("outer_in"), &sink.inputs().number);
}

TEST(Group, ExposeErrors) {
    atp::group g("root");
    g.make<sink_module>("sink");
    g.expose_input("in", "sink.number");
    EXPECT_THROW(g.expose_input("in", "sink.number"), std::runtime_error);   // duplicate alias
    EXPECT_THROW(g.expose_input("x", "nobody.number"), std::runtime_error);  // no such child
    EXPECT_THROW(g.expose_input("y", "sink.missing"), std::runtime_error);   // no such port
    EXPECT_THROW(g.expose_input("z", "sink"), std::invalid_argument);        // a path without a dot
    EXPECT_THROW((void)g.inputs().at("missing"), std::runtime_error);
}

TEST(Group, ConnectsByPathsAcrossSubgroupBoundary) {
    atp::group root("root");
    source_module& src = root.make<source_module>("src");
    atp::group& inner = root.add_group("inner");
    sink_module& sink = inner.make<sink_module>("sink");
    inner.expose_input("in", "sink.number");

    root.connect("src.number", "inner.in");
    ASSERT_EQ(root.connections().size(), 1u);
    EXPECT_EQ(root.connections()[0].out, &src.outputs().number);
    EXPECT_EQ(root.connections()[0].in, &sink.inputs().number);  // the real input, not an alias

    src.outputs().number(7);
    EXPECT_EQ(sink.inputs().number.get(), 7);  // delivered directly, without hops
}

TEST(Group, ConnectReplayDeliversCache) {
    atp::group g("root");
    source_module& src = g.make<source_module>("src");
    sink_module& sink = g.make<sink_module>("sink");
    src.outputs().number(42);  // cached before anything connects
    g.connect("src.number", "sink.number", atp::io::replay);
    EXPECT_EQ(sink.inputs().number.get(), 42);
}

TEST(Group, ConnectErrors) {
    atp::group g("root");
    g.make<source_module>("src");
    g.make<sink_module>("sink");
    EXPECT_THROW(g.connect("src.missing", "sink.number"), std::runtime_error);
    EXPECT_THROW(g.connect("nobody.number", "sink.number"), std::runtime_error);
}

TEST(Group, DestructorDisconnectsItsConnections) {
    source_module src;  // the output outlives the group
    {
        atp::group g("root");
        sink_module& sink = g.make<sink_module>("sink");
        src.outputs().number.connect(sink.inputs().number);  // direct, bypassing the group — the caller has to break it
        src.outputs().number.disconnect(sink.inputs().number);

        source_module& inner_src = g.make<source_module>("inner_src");
        g.connect("inner_src.number", "sink.number");  // recorded by the group
        EXPECT_EQ(inner_src.outputs().number.connections(), 1u);
    }  // the destructor breaks its own record before the children die
    EXPECT_EQ(src.outputs().number.connections(), 0u);
}

}  // namespace
