#include <atomic>
#include <chrono>
#include <latch>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <atp/pipeline.hpp>
#include <atp/pipeline_runner.hpp>

#include "pipeline_test_support.hpp"

namespace {

using atp_tests::event_log;
using atp_tests::probe_module;

// Pipeline root[a, stage[b, deep[c]], d] — the material for cascades and layout.
struct rig {
    atp::pipeline pipe;
    event_log log;
    probe_module* a;
    probe_module* b;
    probe_module* c;
    probe_module* d;
    atp::group* stage;
    atp::group* deep;

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

    atp::pipeline_runner runner;
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

    atp::pipeline_runner runner;
    EXPECT_THROW(runner.start(r.pipe), std::runtime_error);
    EXPECT_FALSE(runner.running());
    // Everyone passed initialize and start reached c, so root.stop() reaches everyone in reverse
    // order — stop is correct after initialize without start.
    std::vector<std::string> reversed{"d", "c", "b", "a"};
    EXPECT_EQ(r.log.order_of("stop"), reversed);
    EXPECT_TRUE(r.log.order_of("iterate").empty());  // no threads were created
}

TEST(PipelineRunner, AssignmentsPlaceGroupsOnNamedThreads) {
    rig r;
    std::latch ticked(4);
    r.a->first_iterate = &ticked;  // root → the first declared thread
    r.b->first_iterate = &ticked;  // stage → "aux", explicitly
    r.c->first_iterate = &ticked;  // deep is unassigned → inline in stage
    r.d->first_iterate = &ticked;  // wait for all: a stop() between a and d would cut the root pass

    atp::pipeline_runner runner;
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
    EXPECT_NE(root_thread, stage_thread);                // different threads
    EXPECT_EQ(r.log.iterate_thread("c"), stage_thread);  // an inline group inherits stage's thread
    EXPECT_EQ(r.log.iterate_thread("d"), root_thread);
}

TEST(PipelineRunner, EmptyConfigurationRunsEverythingOnImplicitMain) {
    rig r;
    // Wait for all four rather than one: a probe signals its latch at the start of its iterate, so
    // waking up mid-pass says nothing about the other children, and a stop() between c and d would
    // cut the root pass short (a group's iterate checks the stop token before every child),
    // leaving d without a log entry. The entries do arrive: stop() joins the threads.
    std::latch ticked(4);
    r.a->first_iterate = &ticked;
    r.b->first_iterate = &ticked;
    r.c->first_iterate = &ticked;
    r.d->first_iterate = &ticked;

    atp::pipeline_runner runner;  // not a single add_thread
    runner.start(r.pipe);
    ticked.wait();
    runner.stop();

    auto t = r.log.iterate_thread("a");
    EXPECT_EQ(r.log.iterate_thread("b"), t);
    EXPECT_EQ(r.log.iterate_thread("c"), t);
    EXPECT_EQ(r.log.iterate_thread("d"), t);
}

TEST(PipelineRunner, ValidatesUnsafeCrossThreadConnectionsWithThreadNames) {
    atp::pipeline pipe;

    struct out_section : atp::io::outputs {
        atp::io::output<int>& value = make<atp::io::output<int>>("value");
    };
    struct in_section : atp::io::inputs {
        atp::io::input<int>& value = make<atp::io::input<int>>("value", atp::io::unsafe);
    };
    using producer_ports = atp::io::ports<atp::io::inputs, out_section>;
    using consumer_ports = atp::io::ports<in_section>;
    class producer : public atp::module<producer_ports> {};
    class consumer : public atp::module<consumer_ports> {};

    atp::group& left = pipe.root().add_group("left");
    left.make<producer>("p");
    left.expose_output("out", "p.value");
    atp::group& right = pipe.root().add_group("right");
    right.make<consumer>("c");
    right.expose_input("in", "c.value");
    pipe.root().connect("left.out", "right.in");

    atp::pipeline_runner split;
    split.add_thread("producing");
    split.add_thread("consuming");
    split.assign(left, "producing");
    split.assign(right, "consuming");
    try {
        split.start(pipe);
        FAIL() << "expected std::runtime_error";
    } catch (const std::runtime_error& error) {
        const std::string what = error.what();  // the thread names belong in the diagnostics
        EXPECT_NE(what.find("producing"), std::string::npos);
        EXPECT_NE(what.find("consuming"), std::string::npos);
    }

    atp::pipeline_runner together;  // the same groups on one thread are fine
    together.start(pipe);
    together.stop();
}

TEST(PipelineRunner, ConfigurationErrors) {
    rig r;
    atp::pipeline_runner runner;
    runner.add_thread("main");
    EXPECT_THROW(runner.add_thread("main"), std::runtime_error);  // duplicate name
    EXPECT_THROW(runner.add_thread("t", {atp::thread_mode::throttled, {}}),
                 std::invalid_argument);  // the period is required
    EXPECT_THROW(runner.add_thread("s", {atp::thread_mode::on_demand, std::chrono::milliseconds(5)}),
                 std::invalid_argument);                                      // the period is forbidden
    EXPECT_THROW(runner.assign(*r.stage, "nowhere"), std::invalid_argument);  // unknown name, rejected at once

    atp::group stranger("stranger");
    runner.assign(stranger, "main");
    EXPECT_THROW(runner.start(r.pipe), std::invalid_argument);  // an assignment outside the tree
}

// A failed validation must leave no traces in the runner: reassigning and starting again have to
// work — the "not running means clean state" invariant, observed from outside through reuse.
TEST(PipelineRunner, FailedValidationLeavesRunnerReusable) {
    atp::pipeline pipe;

    struct out_section : atp::io::outputs {
        atp::io::output<int>& value = make<atp::io::output<int>>("value");
    };
    struct in_section : atp::io::inputs {
        atp::io::input<int>& value = make<atp::io::input<int>>("value", atp::io::unsafe);
    };
    using producer_ports = atp::io::ports<atp::io::inputs, out_section>;
    using consumer_ports = atp::io::ports<in_section>;
    class producer : public atp::module<producer_ports> {};
    class consumer : public atp::module<consumer_ports> {};

    atp::group& left = pipe.root().add_group("left");
    left.make<producer>("p");
    left.expose_output("out", "p.value");
    atp::group& right = pipe.root().add_group("right");
    right.make<consumer>("c");
    right.expose_input("in", "c.value");
    pipe.root().connect("left.out", "right.in");

    atp::pipeline_runner runner;
    runner.add_thread("producing");
    runner.add_thread("consuming");
    runner.assign(left, "producing");
    runner.assign(right, "consuming");
    EXPECT_THROW(runner.start(pipe), std::runtime_error);  // an unsafe input across a thread boundary
    EXPECT_FALSE(runner.running());

    runner.assign(right, "producing");  // reassigned: both groups on one thread, which is valid
    runner.start(pipe);
    EXPECT_TRUE(runner.running());
    runner.stop();
    EXPECT_FALSE(runner.running());
}

TEST(PipelineRunner, IdleThreadBacksOffAndWakesOnData) {
    atp::pipeline pipe;
    std::latch delivered(1);

    struct feed_outputs : atp::io::outputs {
        atp::io::output<int>& value = make<atp::io::output<int>>("value");
    };
    struct drain_inputs : atp::io::inputs {
        atp::io::queued_input<int>& value = make<atp::io::queued_input<int>>("value");
    };
    using feed_ports = atp::io::ports<atp::io::inputs, feed_outputs>;
    using drain_ports = atp::io::ports<drain_inputs>;
    // The source stays silent until the test says go; the consumer counts passes.
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

    atp::group& left = pipe.root().add_group("left");
    gated_source& src = left.make<gated_source>("src");
    left.expose_output("out", "src.value");
    atp::group& right = pipe.root().add_group("right");
    counting_sink& sink = right.make<counting_sink>("sink");
    sink.delivered = &delivered;
    right.expose_input("in", "sink.value");
    pipe.root().connect("left.out", "right.in");

    atp::pipeline_runner runner;
    runner.add_thread("producing");
    runner.add_thread("consuming");
    runner.assign(left, "producing");
    runner.assign(right, "consuming");
    runner.start(pipe);

    // An idle window: sleeping is appropriate here — the test observes the pace of an idling
    // thread, and there is nothing to wake it with, nor any reason to.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const int idle_passes = sink.passes.load();
    src.go = true;
    delivered.wait();
    runner.stop();

    // A busy loop would yield millions of passes in 100 ms; the backoff yields dozens.
    EXPECT_LT(idle_passes, 1000);
}

TEST(PipelineRunner, DeliveryWakesIdleConsumerThread) {
    atp::pipeline pipe;

    struct feed_outputs : atp::io::outputs {
        atp::io::output<int>& value = make<atp::io::output<int>>("value");
    };
    struct drain_inputs : atp::io::inputs {
        atp::io::queued_input<int>& value = make<atp::io::queued_input<int>>("value");
    };
    using feed_ports = atp::io::ports<atp::io::inputs, feed_outputs>;
    using drain_ports = atp::io::ports<drain_inputs>;
    // The source spins and fires on command, so its own latency is out of the measurement and only
    // the consumer's wake-up is timed.
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

    atp::group& left = pipe.root().add_group("left");
    gated_source& src = left.make<gated_source>("src");
    left.expose_output("out", "src.value");
    atp::group& right = pipe.root().add_group("right");
    counting_sink& sink = right.make<counting_sink>("sink");
    right.expose_input("in", "sink.value");
    pipe.root().connect("left.out", "right.in");

    atp::pipeline_runner runner;
    runner.add_thread("producing", {atp::thread_mode::spinning});
    runner.add_thread("consuming");  // on_demand — this is the one being woken
    runner.assign(left, "producing");
    runner.assign(right, "consuming");
    runner.start(pipe);

    // One round: let the consumer sink into a deep backoff (capped at 10 ms), then deliver and
    // measure when it noticed. Without wake-ups a round averages ~5-10 ms and the total ~100-200 ms;
    // with them the whole set takes single-digit milliseconds. The threshold sits in between:
    // generous bounds catching the order of magnitude rather than the noise.
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

    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(total).count(), 60);
}

TEST(PipelineRunner, StatsCountPassesPerThread) {
    atp::pipeline pipe;
    std::latch ticked(2);

    // The latch guarantees the first pass of every thread: without it an idle thread might not make
    // it before stop() and its pass count would be zero.
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

    atp::group& left = pipe.root().add_group("left");
    counting_module& busy = left.make<counting_module>("busy");
    busy.status = atp::work_status::busy;
    busy.first = &ticked;
    atp::group& right = pipe.root().add_group("right");
    counting_module& idle = right.make<counting_module>("idle");
    idle.first = &ticked;

    atp::pipeline_runner runner;
    EXPECT_TRUE(runner.stats().empty());  // no counters before the first run

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
    EXPECT_EQ(stats[1].busy_passes, 0u);  // an idle thread reports no busy passes
}

TEST(PipelineRunner, ThrottledPacesIterations) {
    atp::pipeline pipe;
    class counting_module : public atp::module<> {
       public:
        std::atomic<int> passes{0};
        atp::work_status iterate(std::stop_token) override {
            ++passes;
            return atp::work_status::busy;  // busy does not speed a throttled thread up
        }
    };
    atp::group& g = pipe.root().add_group("paced");
    counting_module& m = g.make<counting_module>("m");

    atp::pipeline_runner runner;
    runner.add_thread("paced", {atp::thread_mode::throttled, std::chrono::milliseconds(20)});
    runner.assign(g, "paced");
    runner.start(pipe);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    runner.stop();

    // ~10 ticks in 200 ms; generous bounds catching the order of magnitude rather than the noise.
    EXPECT_GT(m.passes.load(), 2);
    EXPECT_LT(m.passes.load(), 40);
}

TEST(PipelineRunner, SpinningThreadIteratesWithoutSleep) {
    atp::pipeline pipe;
    class idle_counter : public atp::module<> {
       public:
        std::atomic<int> passes{0};
        atp::work_status iterate(std::stop_token) override {
            ++passes;
            return atp::work_status::idle;  // a spinning thread ignores idle and never sleeps
        }
    };
    atp::group& g = pipe.root().add_group("hot");
    idle_counter& m = g.make<idle_counter>("m");

    atp::pipeline_runner runner;
    runner.add_thread("hot", {atp::thread_mode::spinning});
    runner.assign(g, "hot");
    runner.start(pipe);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    runner.stop();

    EXPECT_GT(m.passes.load(), 1000);  // an idle module, yet the thread keeps spinning
}

TEST(PipelineRunner, IterateFailureStopsPipelineAndWaitRethrows) {
    rig r;
    r.b->throw_in = "iterate";

    atp::pipeline_runner runner;
    runner.add_thread("main");
    runner.add_thread("aux");
    runner.assign(*r.stage, "aux");
    runner.start(r.pipe);
    EXPECT_THROW(runner.wait(), std::runtime_error);  // the root cause came from b
    EXPECT_FALSE(runner.running());
    EXPECT_NE(runner.error(), nullptr);
    // the stop cascade reached everyone in reverse order despite the error
    std::vector<std::string> reversed{"d", "c", "b", "a"};
    EXPECT_EQ(r.log.order_of("stop"), reversed);
}

TEST(PipelineRunner, FirstErrorWins) {
    rig r;
    r.a->throw_in = "iterate";  // both throw; exactly one error is kept — the first
    r.b->throw_in = "iterate";

    atp::pipeline_runner runner;
    runner.add_thread("main");
    runner.add_thread("aux");
    runner.assign(*r.stage, "aux");
    runner.start(r.pipe);
    EXPECT_THROW(runner.wait(), std::runtime_error);
    EXPECT_NE(runner.error(), nullptr);  // the slot is filled once
}

TEST(PipelineRunner, StopIsIdempotentAndErrorIsClean) {
    rig r;
    std::latch ticked(1);
    r.a->first_iterate = &ticked;

    atp::pipeline_runner runner;
    runner.start(r.pipe);
    ticked.wait();
    runner.stop();
    runner.stop();  // the second call is a no-op
    EXPECT_EQ(runner.error(), nullptr);
}

TEST(PipelineRunner, WaitAfterStopRethrowsPendingError) {
    rig r;
    std::latch reached(1);
    r.a->first_iterate = &reached;  // the probe signals the latch before throwing
    r.a->throw_in = "iterate";

    atp::pipeline_runner runner;
    runner.start(r.pipe);
    reached.wait();
    runner.stop();                                    // never throws; the error is captured after the join
    EXPECT_THROW(runner.wait(), std::runtime_error);  // stop() did not swallow the root cause
    EXPECT_NE(runner.error(), nullptr);
}

TEST(PipelineRunner, WaitOnIdleRunnerIsNoOp) {
    atp::pipeline_runner runner;
    runner.wait();  // never started and no error — returns at once
    EXPECT_EQ(runner.error(), nullptr);
}

TEST(PipelineRunner, SecondWaitRethrowsSameError) {
    rig r;
    r.b->throw_in = "iterate";

    atp::pipeline_runner runner;
    runner.start(r.pipe);
    EXPECT_THROW(runner.wait(), std::runtime_error);
    EXPECT_THROW(runner.wait(), std::runtime_error);  // the error is kept until the next start()
}

TEST(PipelineRunner, DestructorStopsRunningPipeline) {
    rig r;
    std::latch ticked(1);
    r.a->first_iterate = &ticked;
    {
        atp::pipeline_runner runner;
        runner.start(r.pipe);
        ticked.wait();
    }  // ~pipeline_runner → stop()
    std::vector<std::string> reversed{"d", "c", "b", "a"};
    EXPECT_EQ(r.log.order_of("stop"), reversed);
}

TEST(PipelineRunner, DataFlowsBetweenThreadsThroughExposedPorts) {
    atp::pipeline pipe;
    std::latch delivered(1);

    struct out_section : atp::io::outputs {
        atp::io::output<int>& value = make<atp::io::output<int>>("value");
    };
    struct in_section : atp::io::inputs {
        atp::io::input<int>& value = make<atp::io::input<int>>("value");  // safe is the default
    };
    using producer_ports = atp::io::ports<atp::io::inputs, out_section>;
    using consumer_ports = atp::io::ports<in_section>;
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
        std::atomic<int> received{0};  // the watcher takes the value, so the input is empty after poll
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

    atp::group& left = pipe.root().add_group("left");
    left.make<producer>("p");
    left.expose_output("out", "p.value");
    atp::group& right = pipe.root().add_group("right");
    consumer& c = right.make<consumer>("c");
    c.delivered = &delivered;
    right.expose_input("in", "c.value");
    pipe.root().connect("left.out", "right.in");

    atp::pipeline_runner runner;
    runner.add_thread("producing");
    runner.add_thread("consuming");
    runner.assign(left, "producing");
    runner.assign(right, "consuming");
    runner.start(pipe);
    delivered.wait();
    runner.stop();

    EXPECT_EQ(c.received.load(), 42);
}

}  // namespace
