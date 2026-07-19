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

// Пайплайн: root[a, stage[b, deep[c]], d] — каскады и раскладка.
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
    // initialize прошли все, start дошёл до c — root.stop() получают все,
    // в обратном порядке (stop корректен после initialize без start)
    std::vector<std::string> reversed{"d", "c", "b", "a"};
    EXPECT_EQ(r.log.order_of("stop"), reversed);
    EXPECT_TRUE(r.log.order_of("iterate").empty());   // потоки не создавались
}

TEST(PipelineRunner, AssignmentsPlaceGroupsOnNamedThreads) {
    rig r;
    std::latch ticked(4);
    r.a->first_iterate = &ticked;   // root → первый объявленный поток
    r.b->first_iterate = &ticked;   // stage → "aux" (явно)
    r.c->first_iterate = &ticked;   // deep не назначен → inline у stage
    r.d->first_iterate = &ticked;   // ждём всех: stop() между a и d срезал бы пасс корня

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
    EXPECT_NE(root_thread, stage_thread);                       // разные потоки
    EXPECT_EQ(r.log.iterate_thread("c"), stage_thread);         // inline наследует поток stage
    EXPECT_EQ(r.log.iterate_thread("d"), root_thread);
}

TEST(PipelineRunner, EmptyConfigurationRunsEverythingOnImplicitMain) {
    rig r;
    std::latch ticked(1);
    r.c->first_iterate = &ticked;

    atp::pipeline_runner runner;                                 // ни одного add_thread
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

    struct out_ports : atp::io::outputs {
        atp::io::output<int>& value = make<atp::io::output<int>>("value");
    };
    struct in_ports : atp::io::inputs {
        atp::io::input<int>& value = make<atp::io::input<int>>("value", atp::io::unsafe);
    };
    class producer : public atp::module<atp::io::inputs, out_ports> {};
    class consumer : public atp::module<in_ports, atp::io::outputs> {};

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
        const std::string what = error.what();                   // имена потоков — в диагностике
        EXPECT_NE(what.find("producing"), std::string::npos);
        EXPECT_NE(what.find("consuming"), std::string::npos);
    }

    atp::pipeline_runner together;                               // те же группы на одном потоке — ок
    together.start(pipe);
    together.stop();
}

TEST(PipelineRunner, ConfigurationErrors) {
    rig r;
    atp::pipeline_runner runner;
    runner.add_thread("main");
    EXPECT_THROW(runner.add_thread("main"), std::runtime_error);                       // дубликат имени
    EXPECT_THROW(runner.add_thread("t", {atp::thread_mode::throttled, {}}), std::invalid_argument);  // период обязателен
    EXPECT_THROW(runner.add_thread("s", {atp::thread_mode::on_demand, std::chrono::milliseconds(5)}),
                 std::invalid_argument);                                               // период запрещён
    EXPECT_THROW(runner.assign(*r.stage, "nowhere"), std::invalid_argument);           // неизвестное имя — сразу

    atp::group stranger("stranger");
    runner.assign(stranger, "main");
    EXPECT_THROW(runner.start(r.pipe), std::invalid_argument);                         // назначение вне дерева
}

// Сбой валидации не должен оставлять в раннере следов: переназначение
// и повторный start обязаны работать (инвариант «не running — состояние
// чистое», проверяемый снаружи через переиспользование).
TEST(PipelineRunner, FailedValidationLeavesRunnerReusable) {
    atp::pipeline pipe;

    struct out_ports : atp::io::outputs {
        atp::io::output<int>& value = make<atp::io::output<int>>("value");
    };
    struct in_ports : atp::io::inputs {
        atp::io::input<int>& value = make<atp::io::input<int>>("value", atp::io::unsafe);
    };
    class producer : public atp::module<atp::io::inputs, out_ports> {};
    class consumer : public atp::module<in_ports, atp::io::outputs> {};

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
    EXPECT_THROW(runner.start(pipe), std::runtime_error);  // unsafe вход через границу потоков
    EXPECT_FALSE(runner.running());

    runner.assign(right, "producing");  // переназначение: обе группы на одном потоке — валидно
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
    // Источник молчит до отмашки теста; потребитель считает пассы.
    class gated_source : public atp::module<atp::io::inputs, feed_outputs> {
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
    class counting_sink : public atp::module<drain_inputs, atp::io::outputs> {
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

    // Окно простоя: sleep уместен — тест наблюдает темп простаивающего
    // потока, будить его нечем и незачем.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const int idle_passes = sink.passes.load();
    src.go = true;
    delivered.wait();
    runner.stop();

    // Busy-loop дал бы миллионы пассов за 100 мс; backoff — десятки.
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
    // Источник спинится и стреляет по отмашке — его собственная латентность
    // из замера исключена, меряется только пробуждение потребителя.
    class gated_source : public atp::module<atp::io::inputs, feed_outputs> {
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
    class counting_sink : public atp::module<drain_inputs, atp::io::outputs> {
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
    runner.add_thread("consuming");  // on_demand — его и будим
    runner.assign(left, "producing");
    runner.assign(right, "consuming");
    runner.start(pipe);

    // Раунд: дать потребителю уйти в глубокий backoff (потолок 10 мс), затем
    // доставить и померить, когда заметил. Без пробуждения средний раунд —
    // ~5-10 мс, сумма ~100-200 мс; с пробуждением — единицы мс на все раунды.
    // Порог посередине: границы щедрые — ловим порядок величины, не шум.
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

TEST(PipelineRunner, ThrottledPacesIterations) {
    atp::pipeline pipe;
    class counting_module : public atp::module<atp::io::inputs, atp::io::outputs> {
       public:
        std::atomic<int> passes{0};
        atp::work_status iterate(std::stop_token) override {
            ++passes;
            return atp::work_status::busy;  // busy не разгоняет throttled — темп задаёт период
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

    // ~10 тиков за 200 мс; границы щедрые — ловим порядок величины, не шум.
    EXPECT_GT(m.passes.load(), 2);
    EXPECT_LT(m.passes.load(), 40);
}

TEST(PipelineRunner, SpinningThreadIteratesWithoutSleep) {
    atp::pipeline pipe;
    class idle_counter : public atp::module<atp::io::inputs, atp::io::outputs> {
       public:
        std::atomic<int> passes{0};
        atp::work_status iterate(std::stop_token) override {
            ++passes;
            return atp::work_status::idle;  // spinning игнорирует idle — не спит
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

    EXPECT_GT(m.passes.load(), 1000);  // idle-модуль, но поток крутится
}

TEST(PipelineRunner, IterateFailureStopsPipelineAndWaitRethrows) {
    rig r;
    r.b->throw_in = "iterate";

    atp::pipeline_runner runner;
    runner.add_thread("main");
    runner.add_thread("aux");
    runner.assign(*r.stage, "aux");
    runner.start(r.pipe);
    EXPECT_THROW(runner.wait(), std::runtime_error);   // первопричина — из b
    EXPECT_FALSE(runner.running());
    EXPECT_NE(runner.error(), nullptr);
    // каскад stop прошёл всем в обратном порядке, несмотря на ошибку
    std::vector<std::string> reversed{"d", "c", "b", "a"};
    EXPECT_EQ(r.log.order_of("stop"), reversed);
}

TEST(PipelineRunner, FirstErrorWins) {
    rig r;
    r.a->throw_in = "iterate";   // оба бросают; ошибка ровно одна — первая
    r.b->throw_in = "iterate";

    atp::pipeline_runner runner;
    runner.add_thread("main");
    runner.add_thread("aux");
    runner.assign(*r.stage, "aux");
    runner.start(r.pipe);
    EXPECT_THROW(runner.wait(), std::runtime_error);
    EXPECT_NE(runner.error(), nullptr);                // слот заполнен один раз
}

TEST(PipelineRunner, StopIsIdempotentAndErrorIsClean) {
    rig r;
    std::latch ticked(1);
    r.a->first_iterate = &ticked;

    atp::pipeline_runner runner;
    runner.start(r.pipe);
    ticked.wait();
    runner.stop();
    runner.stop();                                     // второй вызов — no-op
    EXPECT_EQ(runner.error(), nullptr);
}

TEST(PipelineRunner, WaitAfterStopRethrowsPendingError) {
    rig r;
    std::latch reached(1);
    r.a->first_iterate = &reached;   // зонд сигналит latch до броска (см. probe_module)
    r.a->throw_in = "iterate";

    atp::pipeline_runner runner;
    runner.start(r.pipe);
    reached.wait();
    runner.stop();                                     // не бросает; после join ошибка захвачена
    EXPECT_THROW(runner.wait(), std::runtime_error);   // stop() не съел первопричину
    EXPECT_NE(runner.error(), nullptr);
}

TEST(PipelineRunner, WaitOnIdleRunnerIsNoOp) {
    atp::pipeline_runner runner;
    runner.wait();                                     // не стартовал, ошибки нет — сразу возврат
    EXPECT_EQ(runner.error(), nullptr);
}

TEST(PipelineRunner, SecondWaitRethrowsSameError) {
    rig r;
    r.b->throw_in = "iterate";

    atp::pipeline_runner runner;
    runner.start(r.pipe);
    EXPECT_THROW(runner.wait(), std::runtime_error);
    EXPECT_THROW(runner.wait(), std::runtime_error);   // ошибка хранится до следующего start()
}

TEST(PipelineRunner, DestructorStopsRunningPipeline) {
    rig r;
    std::latch ticked(1);
    r.a->first_iterate = &ticked;
    {
        atp::pipeline_runner runner;
        runner.start(r.pipe);
        ticked.wait();
    }                                                  // ~pipeline_runner → stop()
    std::vector<std::string> reversed{"d", "c", "b", "a"};
    EXPECT_EQ(r.log.order_of("stop"), reversed);
}

TEST(PipelineRunner, DataFlowsBetweenThreadsThroughExposedPorts) {
    atp::pipeline pipe;
    std::latch delivered(1);

    struct out_ports : atp::io::outputs {
        atp::io::output<int>& value = make<atp::io::output<int>>("value");
    };
    struct in_ports : atp::io::inputs {
        atp::io::input<int>& value = make<atp::io::input<int>>("value");   // safe — умолчание
    };
    class producer : public atp::module<atp::io::inputs, out_ports> {
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
    class consumer : public atp::module<in_ports, atp::io::outputs> {
       public:
        std::latch* delivered = nullptr;
        std::atomic<int> received{0};  // watcher изымает значение (take) — вход после poll пуст
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
