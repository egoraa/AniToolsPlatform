// SPDX-License-Identifier: Apache-2.0
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <latch>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <atp/runtime/pipeline.hpp>
#include <atp/runtime/pipeline_runner.hpp>

#include "support/pipeline_test_support.hpp"

namespace {

using atp_tests::event_log;
using atp_tests::probe_module;

struct rig {
    atp::runtime::pipeline pipe;
    event_log log;
    probe_module* a;
    probe_module* b;
    probe_module* c;
    probe_module* d;
    atp::runtime::group* stage;
    atp::runtime::group* deep;

    rig() {
        a = &pipe.root().make<probe_module>("a", log, "a");
        stage = &pipe.root().add_group("stage");
        b = &stage->make<probe_module>("b", log, "b");
        deep = &stage->add_group("deep");
        c = &deep->make<probe_module>("c", log, "c");
        d = &pipe.root().make<probe_module>("d", log, "d");
    }
};

TEST(PipelineRunner, CascadesRunThroughRunnerAndReverseOnStop) {
    rig r;
    std::latch ticked(1);
    r.a->first_iterate = &ticked;

    atp::runtime::pipeline_runner runner;
    runner.start(r.pipe);
    ticked.wait();
    runner.stop();

    std::vector<std::string> expected{"a", "b", "c", "d"};
    EXPECT_EQ(r.log.order_of("initialize"), expected);
    EXPECT_EQ(r.log.order_of("start"), expected);
    std::vector<std::string> reversed{"d", "c", "b", "a"};
    EXPECT_EQ(r.log.order_of("stop"), reversed);
}

TEST(PipelineRunner, StartFailureStopsInitializedInReverse) {
    rig r;
    r.c->throw_in = "start";

    atp::runtime::pipeline_runner runner;
    EXPECT_THROW(runner.start(r.pipe), std::runtime_error);
    EXPECT_FALSE(runner.running());
    std::vector<std::string> reversed{"d", "c", "b", "a"};
    EXPECT_EQ(r.log.order_of("stop"), reversed);
    EXPECT_TRUE(r.log.order_of("iterate").empty());
}

TEST(PipelineRunner, AssignmentsPlaceGroupsOnNamedThreads) {
    rig r;
    std::latch ticked(4);
    r.a->first_iterate = &ticked;
    r.b->first_iterate = &ticked;
    r.c->first_iterate = &ticked;
    r.d->first_iterate = &ticked;

    atp::runtime::pipeline_runner runner;
    runner.add_thread("main");
    runner.add_thread("aux");
    runner.assign(*r.stage, "aux");
    runner.start(r.pipe);
    ticked.wait();
    runner.stop();

    auto root_thread = r.log.iterate_thread("a");
    auto stage_thread = r.log.iterate_thread("b");
    EXPECT_NE(root_thread, std::thread::id{});
    EXPECT_NE(stage_thread, std::thread::id{});
    EXPECT_NE(root_thread, stage_thread);
    EXPECT_EQ(r.log.iterate_thread("c"), stage_thread);
    EXPECT_EQ(r.log.iterate_thread("d"), root_thread);
}

TEST(PipelineRunner, EmptyConfigurationRunsEverythingOnImplicitMain) {
    rig r;
    std::latch ticked(4);
    r.a->first_iterate = &ticked;
    r.b->first_iterate = &ticked;
    r.c->first_iterate = &ticked;
    r.d->first_iterate = &ticked;

    atp::runtime::pipeline_runner runner;
    runner.start(r.pipe);
    ticked.wait();
    runner.stop();

    auto t = r.log.iterate_thread("a");
    EXPECT_EQ(r.log.iterate_thread("b"), t);
    EXPECT_EQ(r.log.iterate_thread("c"), t);
    EXPECT_EQ(r.log.iterate_thread("d"), t);
}

TEST(PipelineRunner, ValidatesUnsafeCrossThreadConnectionsWithThreadNames) {
    atp::runtime::pipeline pipe;

    struct out_section : atp::io::outputs {
        atp::io::output<int>& value = make<atp::io::output<int>>("value");
    };
    struct in_section : atp::io::inputs {
        atp::io::input<int>& value = make<atp::io::input<int>>("value", atp::io::unsafe);
    };
    using producer_ports = atp::ports<atp::io::inputs, out_section>;
    using consumer_ports = atp::ports<in_section>;
    class producer : public atp::module<producer_ports> {};
    class consumer : public atp::module<consumer_ports> {};

    atp::runtime::group& left = pipe.root().add_group("left");
    left.make<producer>("p");
    left.expose_output("out", "p.value");
    atp::runtime::group& right = pipe.root().add_group("right");
    right.make<consumer>("c");
    right.expose_input("in", "c.value");
    pipe.root().connect("left.out", "right.in");

    atp::runtime::pipeline_runner split;
    split.add_thread("producing");
    split.add_thread("consuming");
    split.assign(left, "producing");
    split.assign(right, "consuming");
    try {
        split.start(pipe);
        FAIL() << "expected std::runtime_error";
    } catch (const std::runtime_error& error) {
        const std::string what = error.what();
        EXPECT_NE(what.find("producing"), std::string::npos);
        EXPECT_NE(what.find("consuming"), std::string::npos);
    }

    atp::runtime::pipeline_runner together;
    together.start(pipe);
    together.stop();
}

TEST(PipelineRunner, ConfigurationErrors) {
    rig r;
    atp::runtime::pipeline_runner runner;
    runner.add_thread("main");
    EXPECT_THROW(runner.add_thread("main"), std::runtime_error);
    EXPECT_THROW(runner.add_thread("t", {atp::runtime::thread_mode::throttled, {}}), std::invalid_argument);
    EXPECT_THROW(runner.add_thread("s", {atp::runtime::thread_mode::on_demand, std::chrono::milliseconds(5)}),
                 std::invalid_argument);
    EXPECT_THROW(runner.assign(*r.stage, "nowhere"), std::invalid_argument);

    atp::runtime::group stranger("stranger");
    runner.assign(stranger, "main");
    EXPECT_THROW(runner.start(r.pipe), std::invalid_argument);
}

TEST(PipelineRunner, FailedValidationLeavesRunnerReusable) {
    atp::runtime::pipeline pipe;

    struct out_section : atp::io::outputs {
        atp::io::output<int>& value = make<atp::io::output<int>>("value");
    };
    struct in_section : atp::io::inputs {
        atp::io::input<int>& value = make<atp::io::input<int>>("value", atp::io::unsafe);
    };
    using producer_ports = atp::ports<atp::io::inputs, out_section>;
    using consumer_ports = atp::ports<in_section>;
    class producer : public atp::module<producer_ports> {};
    class consumer : public atp::module<consumer_ports> {};

    atp::runtime::group& left = pipe.root().add_group("left");
    left.make<producer>("p");
    left.expose_output("out", "p.value");
    atp::runtime::group& right = pipe.root().add_group("right");
    right.make<consumer>("c");
    right.expose_input("in", "c.value");
    pipe.root().connect("left.out", "right.in");

    atp::runtime::pipeline_runner runner;
    runner.add_thread("producing");
    runner.add_thread("consuming");
    runner.assign(left, "producing");
    runner.assign(right, "consuming");
    EXPECT_THROW(runner.start(pipe), std::runtime_error);
    EXPECT_FALSE(runner.running());

    runner.assign(right, "producing");
    runner.start(pipe);
    EXPECT_TRUE(runner.running());
    runner.stop();
    EXPECT_FALSE(runner.running());
}

TEST(PipelineRunner, IdleThreadBacksOffAndWakesOnData) {
    atp::runtime::pipeline pipe;
    std::latch delivered(1);

    struct feed_outputs : atp::io::outputs {
        atp::io::output<int>& value = make<atp::io::output<int>>("value");
    };
    struct drain_inputs : atp::io::inputs {
        atp::io::queued_input<int>& value = make<atp::io::queued_input<int>>("value");
    };
    using feed_ports = atp::ports<atp::io::inputs, feed_outputs>;
    using drain_ports = atp::ports<drain_inputs>;
    class gated_source : public atp::module<feed_ports> {
       public:
        std::atomic<bool> go{false};
        atp::work_status iterate(std::stop_token) override {
            if (!go.exchange(false)) {
                return atp::work_status::idle;
            }
            outputs().value(7);
            return atp::work_status::busy;
        }
    };
    class counting_sink : public atp::module<drain_ports> {
       public:
        std::latch* delivered = nullptr;
        std::atomic<int> passes{0};
        atp::work_status iterate(std::stop_token) override {
            ++passes;
            if (inputs().value.try_pop()) {
                delivered->count_down();
                return atp::work_status::busy;
            }
            return atp::work_status::idle;
        }
    };

    atp::runtime::group& left = pipe.root().add_group("left");
    gated_source& src = left.make<gated_source>("src");
    left.expose_output("out", "src.value");
    atp::runtime::group& right = pipe.root().add_group("right");
    counting_sink& sink = right.make<counting_sink>("sink");
    sink.delivered = &delivered;
    right.expose_input("in", "sink.value");
    pipe.root().connect("left.out", "right.in");

    atp::runtime::pipeline_runner runner;
    runner.add_thread("producing");
    runner.add_thread("consuming");
    runner.assign(left, "producing");
    runner.assign(right, "consuming");
    runner.start(pipe);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const int idle_passes = sink.passes.load();
    src.go = true;
    delivered.wait();
    runner.stop();

    EXPECT_LT(idle_passes, 1000);
}

TEST(PipelineRunner, DeliveryWakesIdleConsumerThread) {
    atp::runtime::pipeline pipe;

    struct feed_outputs : atp::io::outputs {
        atp::io::output<int>& value = make<atp::io::output<int>>("value");
    };
    struct drain_inputs : atp::io::inputs {
        atp::io::queued_input<int>& value = make<atp::io::queued_input<int>>("value");
    };
    using feed_ports = atp::ports<atp::io::inputs, feed_outputs>;
    using drain_ports = atp::ports<drain_inputs>;
    class gated_source : public atp::module<feed_ports> {
       public:
        std::atomic<bool> go{false};
        atp::work_status iterate(std::stop_token) override {
            if (!go.exchange(false)) {
                return atp::work_status::idle;
            }
            outputs().value(1);
            return atp::work_status::busy;
        }
    };
    class counting_sink : public atp::module<drain_ports> {
       public:
        std::atomic<int> received{0};
        atp::work_status iterate(std::stop_token) override {
            if (inputs().value.try_pop()) {
                received.fetch_add(1);
                received.notify_all();
                return atp::work_status::busy;
            }
            return atp::work_status::idle;
        }
    };

    atp::runtime::group& left = pipe.root().add_group("left");
    gated_source& src = left.make<gated_source>("src");
    left.expose_output("out", "src.value");
    atp::runtime::group& right = pipe.root().add_group("right");
    counting_sink& sink = right.make<counting_sink>("sink");
    right.expose_input("in", "sink.value");
    pipe.root().connect("left.out", "right.in");

    atp::runtime::pipeline_runner runner;
    runner.add_thread("producing", {atp::runtime::thread_mode::spinning});
    runner.add_thread("consuming");
    runner.assign(left, "producing");
    runner.assign(right, "consuming");
    runner.start(pipe);

    constexpr int rounds = 20;
    std::chrono::steady_clock::duration total{};
    for (int i = 0; i < rounds; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        const auto sent = std::chrono::steady_clock::now();
        src.go = true;
        int seen = sink.received.load();
        while (seen < i + 1) {
            sink.received.wait(seen);
            seen = sink.received.load();
        }
        total += std::chrono::steady_clock::now() - sent;
    }
    runner.stop();

    // The property under test is that the notifier beats the idle backoff, and the margin for that
    // is wide: without one these rounds would cost up to rounds * idle_sleep_cap = 200 ms. The bound
    // is kept well below that rather than close to the measured time, because this is wall clock on
    // a shared CI runner — a threshold with no headroom fails on scheduling noise, not on a
    // regression.
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(total).count(), 120);
}

TEST(PipelineRunner, StatsCountPassesPerThread) {
    atp::runtime::pipeline pipe;
    std::latch ticked(2);

    class counting_module : public atp::module<> {
       public:
        std::latch* first = nullptr;
        atp::work_status status = atp::work_status::idle;
        atp::work_status iterate(std::stop_token) override {
            if (first != nullptr) {
                first->count_down();
                first = nullptr;
            }
            return status;
        }
    };

    atp::runtime::group& left = pipe.root().add_group("left");
    counting_module& busy = left.make<counting_module>("busy");
    busy.status = atp::work_status::busy;
    busy.first = &ticked;
    atp::runtime::group& right = pipe.root().add_group("right");
    counting_module& idle = right.make<counting_module>("idle");
    idle.first = &ticked;

    atp::runtime::pipeline_runner runner;
    EXPECT_TRUE(runner.stats().empty());

    runner.add_thread("working");
    runner.add_thread("idling");
    runner.assign(left, "working");
    runner.assign(right, "idling");
    runner.start(pipe);
    ticked.wait();
    runner.stop();

    const auto stats = runner.stats();
    ASSERT_EQ(stats.size(), 2u);
    EXPECT_EQ(stats[0].name, "working");
    EXPECT_EQ(stats[1].name, "idling");
    EXPECT_GE(stats[0].passes, 1u);
    EXPECT_GE(stats[0].busy_passes, 1u);
    EXPECT_LE(stats[0].busy_passes, stats[0].passes);
    EXPECT_GE(stats[1].passes, 1u);
    EXPECT_EQ(stats[1].busy_passes, 0u);
}

TEST(PipelineRunner, StatsAreMonotonicAcrossRestarts) {
    atp::runtime::pipeline pipe;

    class ticking_module : public atp::module<> {
       public:
        std::latch* first = nullptr;
        atp::work_status iterate(std::stop_token) override {
            if (first != nullptr) {
                first->count_down();
                first = nullptr;
            }
            return atp::work_status::busy;
        }
    };

    ticking_module& ticking = pipe.root().make<ticking_module>("ticking");
    atp::runtime::pipeline_runner runner;
    runner.add_thread("t", {atp::runtime::thread_mode::throttled, std::chrono::milliseconds(1000)});

    std::latch first_pass(1);
    ticking.first = &first_pass;
    runner.start(pipe);
    first_pass.wait();
    runner.stop();
    const std::uint64_t after_first_run = runner.stats().front().passes;
    ASSERT_GE(after_first_run, 1u);

    std::latch second_pass(1);
    ticking.first = &second_pass;
    runner.start(pipe);
    second_pass.wait();
    runner.stop();

    EXPECT_GT(runner.stats().front().passes, after_first_run)
        << "the pass counters are monotonic for the life of the runner, like every other counter here";
}

TEST(PipelineRunner, ThrottledPacesIterations) {
    constexpr auto period = std::chrono::milliseconds(20);
    atp::runtime::pipeline pipe;
    class ticking_module : public atp::module<> {
       public:
        std::vector<std::chrono::steady_clock::time_point> ticks;
        atp::work_status iterate(std::stop_token) override {
            ticks.push_back(std::chrono::steady_clock::now());
            return atp::work_status::busy;
        }
    };
    atp::runtime::group& g = pipe.root().add_group("paced");
    ticking_module& m = g.make<ticking_module>("m");

    atp::runtime::pipeline_runner runner;
    runner.add_thread("paced", {atp::runtime::thread_mode::throttled, period});
    runner.assign(g, "paced");
    runner.start(pipe);
    std::this_thread::sleep_for(period * 10);
    runner.stop();

    constexpr auto slack = std::chrono::milliseconds(1);
    ASSERT_GE(m.ticks.size(), 2u) << "the paced thread did not iterate twice";
    for (std::size_t i = 1; i < m.ticks.size(); ++i) {
        const auto gap = std::chrono::duration_cast<std::chrono::milliseconds>(m.ticks[i] - m.ticks[i - 1]);
        EXPECT_GE(gap.count(), (period - slack).count())
            << "gap #" << i << " is " << gap.count() << " ms, shorter than the " << period.count() << " ms period";
    }
}

TEST(PipelineRunner, SpinningThreadIteratesWithoutSleep) {
    atp::runtime::pipeline pipe;
    class idle_counter : public atp::module<> {
       public:
        std::atomic<int> passes{0};
        atp::work_status iterate(std::stop_token) override {
            ++passes;
            return atp::work_status::idle;
        }
    };
    atp::runtime::group& g = pipe.root().add_group("hot");
    idle_counter& m = g.make<idle_counter>("m");

    atp::runtime::pipeline_runner runner;
    runner.add_thread("hot", {atp::runtime::thread_mode::spinning});
    runner.assign(g, "hot");
    runner.start(pipe);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    runner.stop();

    EXPECT_GT(m.passes.load(), 1000);
}

TEST(PipelineRunner, IterateFailureStopsPipelineAndWaitRethrows) {
    rig r;
    r.b->throw_in = "iterate";

    atp::runtime::pipeline_runner runner;
    runner.add_thread("main");
    runner.add_thread("aux");
    runner.assign(*r.stage, "aux");
    runner.start(r.pipe);
    EXPECT_THROW(runner.wait(), std::runtime_error);
    EXPECT_FALSE(runner.running());
    EXPECT_NE(runner.error(), nullptr);
    std::vector<std::string> reversed{"d", "c", "b", "a"};
    EXPECT_EQ(r.log.order_of("stop"), reversed);
}

TEST(PipelineRunner, FirstErrorWins) {
    rig r;
    r.a->throw_in = "iterate";
    r.b->throw_in = "iterate";

    atp::runtime::pipeline_runner runner;
    runner.add_thread("main");
    runner.add_thread("aux");
    runner.assign(*r.stage, "aux");
    runner.start(r.pipe);
    EXPECT_THROW(runner.wait(), std::runtime_error);
    EXPECT_NE(runner.error(), nullptr);
}

TEST(PipelineRunner, StopIsIdempotentAndErrorIsClean) {
    rig r;
    std::latch ticked(1);
    r.a->first_iterate = &ticked;

    atp::runtime::pipeline_runner runner;
    runner.start(r.pipe);
    ticked.wait();
    runner.stop();
    runner.stop();
    EXPECT_EQ(runner.error(), nullptr);
}

TEST(PipelineRunner, WaitAfterStopRethrowsPendingError) {
    rig r;
    std::latch reached(1);
    r.a->first_iterate = &reached;
    r.a->throw_in = "iterate";

    atp::runtime::pipeline_runner runner;
    runner.start(r.pipe);
    reached.wait();
    runner.stop();
    EXPECT_THROW(runner.wait(), std::runtime_error);
    EXPECT_NE(runner.error(), nullptr);
}

TEST(PipelineRunner, WaitOnIdleRunnerIsNoOp) {
    atp::runtime::pipeline_runner runner;
    runner.wait();
    EXPECT_EQ(runner.error(), nullptr);
}

TEST(PipelineRunner, SecondWaitRethrowsSameError) {
    rig r;
    r.b->throw_in = "iterate";

    atp::runtime::pipeline_runner runner;
    runner.start(r.pipe);
    EXPECT_THROW(runner.wait(), std::runtime_error);
    EXPECT_THROW(runner.wait(), std::runtime_error);
}

TEST(PipelineRunner, DestructorStopsRunningPipeline) {
    rig r;
    std::latch ticked(1);
    r.a->first_iterate = &ticked;
    {
        atp::runtime::pipeline_runner runner;
        runner.start(r.pipe);
        ticked.wait();
    }
    std::vector<std::string> reversed{"d", "c", "b", "a"};
    EXPECT_EQ(r.log.order_of("stop"), reversed);
}

TEST(PipelineRunner, DataFlowsBetweenThreadsThroughExposedPorts) {
    atp::runtime::pipeline pipe;
    std::latch delivered(1);

    struct out_section : atp::io::outputs {
        atp::io::output<int>& value = make<atp::io::output<int>>("value");
    };
    struct in_section : atp::io::inputs {
        atp::io::input<int>& value = make<atp::io::input<int>>("value");
    };
    using producer_ports = atp::ports<atp::io::inputs, out_section>;
    using consumer_ports = atp::ports<in_section>;
    class producer : public atp::module<producer_ports> {
       public:
        atp::work_status iterate(std::stop_token) override {
            if (sent_) {
                return atp::work_status::idle;
            }
            sent_ = true;
            outputs().value(42);
            return atp::work_status::busy;
        }

       private:
        bool sent_ = false;
    };
    class consumer : public atp::module<consumer_ports> {
       public:
        std::latch* delivered = nullptr;
        std::atomic<int> received{0};
        void initialize(atp::module_context&) override {
            watcher_.watch(inputs().value, [this](const int& value) {
                received = value;
                delivered->count_down();
            });
        }
        atp::work_status iterate(std::stop_token) override {
            return watcher_.poll();
        }

       private:
        atp::io::watcher watcher_;
    };

    atp::runtime::group& left = pipe.root().add_group("left");
    left.make<producer>("p");
    left.expose_output("out", "p.value");
    atp::runtime::group& right = pipe.root().add_group("right");
    consumer& c = right.make<consumer>("c");
    c.delivered = &delivered;
    right.expose_input("in", "c.value");
    pipe.root().connect("left.out", "right.in");

    atp::runtime::pipeline_runner runner;
    runner.add_thread("producing");
    runner.add_thread("consuming");
    runner.assign(left, "producing");
    runner.assign(right, "consuming");
    runner.start(pipe);
    delivered.wait();
    runner.stop();

    EXPECT_EQ(c.received.load(), 42);
}

TEST(PipelineRunner, StopIsNoexceptEvenWithDetachedGroups) {
    static_assert(noexcept(std::declval<atp::runtime::pipeline_runner&>().stop()),
                  "stop() is documented as never throwing, and the destructor relies on it");

    rig r;
    std::latch ticked(2);
    r.a->first_iterate = &ticked;
    r.b->first_iterate = &ticked;

    atp::runtime::pipeline_runner runner;
    runner.add_thread("main");
    runner.add_thread("aux");
    runner.assign(*r.stage, "aux");
    runner.start(r.pipe);
    ticked.wait();

    EXPECT_NO_THROW(runner.stop());
    EXPECT_NO_THROW(runner.stop());
    EXPECT_EQ(runner.error(), nullptr);
}

TEST(PipelineRunner, DestructorStopsAPipelineThatHasDetachedGroups) {
    rig r;
    std::latch ticked(2);
    r.a->first_iterate = &ticked;
    r.b->first_iterate = &ticked;
    {
        atp::runtime::pipeline_runner runner;
        runner.add_thread("main");
        runner.add_thread("aux");
        runner.assign(*r.stage, "aux");
        runner.start(r.pipe);
        ticked.wait();
    }
    const std::vector<std::string> reversed{"d", "c", "b", "a"};
    EXPECT_EQ(r.log.order_of("stop"), reversed);
}

TEST(PipelineRunner, ANameLongerThanThePlatformAllowsDoesNotBringTheThreadDown) {
    rig r;
    std::latch ticked(1);
    r.a->first_iterate = &ticked;

    atp::runtime::pipeline_runner runner;
    runner.add_thread(std::string(512, 'x'));
    runner.start(r.pipe);
    ticked.wait();
    runner.stop();

    EXPECT_EQ(runner.error(), nullptr);
    EXPECT_EQ(runner.stats().size(), 1u);
}

}  // namespace
