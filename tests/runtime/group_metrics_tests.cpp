// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <stop_token>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <atp/runtime/group.hpp>

#include "support/pipeline_test_support.hpp"

namespace {

using atp_tests::event_log;
using atp_tests::probe_module;

class idle_module : public atp::module<> {
   public:
    explicit idle_module(std::string name) : name_(std::move(name)) {}

    [[nodiscard]] std::string_view get_name() const noexcept override {
        return name_;
    }

    atp::work_status iterate(std::stop_token) override {
        return atp::work_status::idle;
    }

   private:
    std::string name_;
};

struct sink_inputs : atp::io::inputs {
    atp::io::queued_input<int>& value = make<atp::io::queued_input<int>>("value");
};
using sink_ports = atp::ports<sink_inputs, atp::io::outputs, atp::io::properties>;

class sink_module : public atp::module<sink_ports> {
   public:
    explicit sink_module(std::string name) : name_(std::move(name)) {}

    [[nodiscard]] std::string_view get_name() const noexcept override {
        return name_;
    }

    atp::work_status iterate(std::stop_token) override {
        return atp::work_status::idle;
    }

   private:
    std::string name_;
};

const atp::runtime::group::module_stats* find(const std::vector<atp::runtime::group::module_stats>& stats,
                                              const std::string& path) {
    const auto it =
        std::ranges::find_if(stats, [&](const atp::runtime::group::module_stats& s) { return s.path == path; });
    return it == stats.end() ? nullptr : &*it;
}

const atp::runtime::group::port_stats* find_port(const std::vector<atp::runtime::group::port_stats>& ports,
                                                 const std::string& path) {
    const auto it =
        std::ranges::find_if(ports, [&](const atp::runtime::group::port_stats& p) { return p.path == path; });
    return it == ports.end() ? nullptr : &*it;
}

TEST(GroupMetrics, AreOffByDefaultAndCountNothing) {
    event_log log;
    atp::runtime::group root("root");
    (void)root.make<probe_module>("a", log, "a");

    EXPECT_FALSE(root.metrics_enabled());
    (void)root.iterate({});
    (void)root.iterate({});

    const std::vector<atp::runtime::group::module_stats> stats = root.metrics();
    const atp::runtime::group::module_stats* a = find(stats, "a");
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->calls, 0U);
    EXPECT_EQ(a->total.count(), 0);
}

TEST(GroupMetrics, CountCallsAndBusyPassesSeparately) {
    event_log log;
    atp::runtime::group root("root");
    (void)root.make<probe_module>("busy", log, "busy");
    (void)root.make<idle_module>("lazy", "lazy");
    root.set_metrics_enabled(true);

    (void)root.iterate({});
    (void)root.iterate({});
    (void)root.iterate({});

    const std::vector<atp::runtime::group::module_stats> stats = root.metrics();
    const atp::runtime::group::module_stats* busy = find(stats, "busy");
    const atp::runtime::group::module_stats* lazy = find(stats, "lazy");
    ASSERT_NE(busy, nullptr);
    ASSERT_NE(lazy, nullptr);
    EXPECT_EQ(busy->calls, 3U);
    EXPECT_EQ(busy->busy_calls, 3U);
    EXPECT_EQ(lazy->calls, 3U);
    EXPECT_EQ(lazy->busy_calls, 0U);
    EXPECT_GE(busy->max.count(), 0);
    EXPECT_GE(busy->total.count(), busy->max.count());
}

TEST(GroupMetrics, ReportNestedChildrenUnderDottedPaths) {
    event_log log;
    atp::runtime::group root("root");
    atp::runtime::group& stage = root.add_group("stage");
    (void)stage.make<probe_module>("deep", log, "deep");
    root.set_metrics_enabled(true);

    (void)root.iterate({});

    const std::vector<atp::runtime::group::module_stats> stats = root.metrics();
    ASSERT_NE(find(stats, "stage"), nullptr);
    ASSERT_NE(find(stats, "stage.deep"), nullptr);
    EXPECT_EQ(find(stats, "stage")->calls, 1U);
    EXPECT_EQ(find(stats, "stage.deep")->calls, 1U);
}

TEST(GroupMetrics, ChargeAGroupForItsWholeSubtree) {
    event_log log;
    atp::runtime::group root("root");
    atp::runtime::group& stage = root.add_group("stage");
    (void)stage.make<probe_module>("deep", log, "deep");
    root.set_metrics_enabled(true);

    for (int i = 0; i < 50; ++i) {
        (void)root.iterate({});
    }

    const std::vector<atp::runtime::group::module_stats> stats = root.metrics();
    EXPECT_GE(find(stats, "stage")->total.count(), find(stats, "stage.deep")->total.count());
}

TEST(GroupMetrics, DoNotChargeADetachedChild) {
    event_log log;
    atp::runtime::group root("root");
    atp::runtime::group& stage = root.add_group("stage");
    (void)stage.make<probe_module>("deep", log, "deep");
    root.set_metrics_enabled(true);
    root.set_detached(stage, true);

    (void)root.iterate({});

    const std::vector<atp::runtime::group::module_stats> stats = root.metrics();
    EXPECT_EQ(find(stats, "stage")->calls, 0U);
    EXPECT_EQ(find(stats, "stage.deep")->calls, 0U);
}

TEST(GroupMetrics, EnablingCascadesIntoSubgroups) {
    atp::runtime::group root("root");
    atp::runtime::group& stage = root.add_group("stage");
    atp::runtime::group& deep = stage.add_group("deep");

    root.set_metrics_enabled(true);
    EXPECT_TRUE(stage.metrics_enabled());
    EXPECT_TRUE(deep.metrics_enabled());

    root.set_metrics_enabled(false, false);
    EXPECT_FALSE(root.metrics_enabled());
    EXPECT_TRUE(stage.metrics_enabled());
}

TEST(GroupInputMetrics, ReportsEveryPortWithItsDottedPath) {
    atp::runtime::group root("root");
    sink_module& sink = root.make<sink_module>("dst", "dst");
    sink.inputs().get<atp::io::queued_input<int>>("value")(7);

    const std::vector<atp::runtime::group::port_stats> ports = root.input_metrics();
    const atp::runtime::group::port_stats* value = find_port(ports, "dst.value");
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->stats.received, 1U);
    EXPECT_EQ(value->stats.pending, 1U);
    EXPECT_EQ(value->stats.capacity, 32U);
}

TEST(GroupInputMetrics, WalksIntoSubgroups) {
    atp::runtime::group root("root");
    atp::runtime::group& stage = root.add_group("stage");
    (void)stage.make<sink_module>("dst", "dst");

    const std::vector<atp::runtime::group::port_stats> ports = root.input_metrics();
    EXPECT_NE(find_port(ports, "stage.dst.value"), nullptr);
}

TEST(GroupInputMetrics, CountsWhatAPortLostToItsCapacity) {
    atp::runtime::group root("root");
    sink_module& sink = root.make<sink_module>("dst", "dst");
    auto& value = sink.inputs().get<atp::io::queued_input<int>>("value");
    for (int i = 0; i < 40; ++i) {
        value(i);
    }

    const std::vector<atp::runtime::group::port_stats> ports = root.input_metrics();
    const atp::runtime::group::port_stats* stats = find_port(ports, "dst.value");
    ASSERT_NE(stats, nullptr);
    EXPECT_EQ(stats->stats.received, 40U);
    EXPECT_EQ(stats->stats.discarded, 8U);
    EXPECT_EQ(stats->stats.peak_pending, 32U);
}

TEST(GroupMetrics, CascadeReachesASubgroupAddedThroughTheErasedPath) {
    atp::runtime::group root("root");
    auto* stage = new atp::runtime::group("stage");
    (void)root.add("stage", atp::module_ptr{stage});
    (void)stage->make<idle_module>("inner", "inner");

    root.set_metrics_enabled(true);

    EXPECT_TRUE(stage->metrics_enabled());
    const std::vector<atp::runtime::group::module_stats> stats = root.metrics();
    EXPECT_NE(find(stats, "stage.inner"), nullptr);
}

}  // namespace
