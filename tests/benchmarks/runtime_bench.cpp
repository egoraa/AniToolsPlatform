// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stop_token>
#include <thread>
#include <vector>

#include <benchmark/benchmark.h>

#include <atp/group.hpp>
#include <atp/module.hpp>
#include <atp/pipeline.hpp>
#include <atp/pipeline_runner.hpp>

namespace {

using clock_type = std::chrono::steady_clock;

struct source_outputs : atp::io::outputs {
    atp::io::output<std::int64_t>& tick = make<atp::io::output<std::int64_t>>("tick");
};
using source_ports = atp::io::ports<atp::io::inputs, source_outputs, atp::io::properties>;

class source_module : public atp::module<source_ports, "bench_source", atp::ver<"1.0">> {
   public:
    atp::work_status iterate(std::stop_token) override {
        outputs().tick(++sent_);
        return atp::work_status::busy;
    }

   private:
    std::int64_t sent_ = 0;
};

struct sink_inputs : atp::io::inputs {
    atp::io::queued_input<std::int64_t>& tick =
        make<atp::io::queued_input<std::int64_t>>("tick", atp::io::drop_oldest(1 << 20));
};
using sink_ports = atp::io::ports<sink_inputs, atp::io::outputs, atp::io::properties>;

class sink_module : public atp::module<sink_ports, "bench_sink", atp::ver<"1.0">> {
   public:
    std::atomic<std::int64_t> seen{0};

    atp::work_status iterate(std::stop_token) override {
        const std::size_t taken = inputs().tick.drain().size();
        if (taken == 0) {
            return atp::work_status::idle;
        }
        seen.fetch_add(static_cast<std::int64_t>(taken), std::memory_order_relaxed);
        return atp::work_status::busy;
    }
};

struct probe_outputs : atp::io::outputs {
    atp::io::output<std::int64_t>& stamp = make<atp::io::output<std::int64_t>>("stamp");
};
using probe_ports = atp::io::ports<atp::io::inputs, probe_outputs, atp::io::properties>;

class probe_source : public atp::module<probe_ports, "bench_probe_source", atp::ver<"1.0">> {
   public:
    std::atomic<bool> armed{false};

    atp::work_status iterate(std::stop_token) override {
        if (!armed.exchange(false, std::memory_order_acq_rel)) {
            return atp::work_status::idle;
        }
        outputs().stamp(clock_type::now().time_since_epoch().count());
        return atp::work_status::busy;
    }
};

struct waker_inputs : atp::io::inputs {
    atp::io::queued_input<std::int64_t>& stamp = make<atp::io::queued_input<std::int64_t>>("stamp");
};
using waker_ports = atp::io::ports<waker_inputs, atp::io::outputs, atp::io::properties>;

class probe_sink : public atp::module<waker_ports, "bench_probe_sink", atp::ver<"1.0">> {
   public:
    std::atomic<std::int64_t> latency_ns{-1};

    atp::work_status iterate(std::stop_token) override {
        const auto sent = inputs().stamp.try_pop();
        if (!sent) {
            return atp::work_status::idle;
        }
        latency_ns.store(clock_type::now().time_since_epoch().count() - *sent, std::memory_order_release);
        return atp::work_status::busy;
    }
};

void run_throughput(benchmark::State& state, bool split_threads) {
    constexpr auto window = std::chrono::milliseconds(200);
    std::int64_t total = 0;
    double seconds = 0.0;

    for (auto _ : state) {
        atp::pipeline pipe;
        sink_module* sink = nullptr;
        if (split_threads) {
            atp::group& producing = pipe.root().add_group("producing");
            atp::group& consuming = pipe.root().add_group("consuming");
            (void)producing.make<source_module>("src");
            sink = &consuming.make<sink_module>("dst");
            producing.expose_output("out", "src.tick");
            consuming.expose_input("in", "dst.tick");
            pipe.root().connect("producing.out", "consuming.in");
        } else {
            (void)pipe.root().make<source_module>("src");
            sink = &pipe.root().make<sink_module>("dst");
            pipe.root().connect("src.tick", "dst.tick");
        }

        atp::pipeline_runner runner;
        runner.add_thread("a", {atp::thread_mode::spinning});
        if (split_threads) {
            runner.add_thread("b", {atp::thread_mode::spinning});
            runner.assign(*pipe.root().find_group("producing"), "a");
            runner.assign(*pipe.root().find_group("consuming"), "b");
        }

        const auto begin = clock_type::now();
        runner.start(pipe);
        std::this_thread::sleep_for(window);
        runner.stop();
        const auto elapsed = clock_type::now() - begin;

        total += sink->seen.load(std::memory_order_relaxed);
        seconds += std::chrono::duration<double>(elapsed).count();
        state.SetIterationTime(std::chrono::duration<double>(elapsed).count());
    }

    state.counters["items_per_second"] =
        benchmark::Counter(static_cast<double>(total) / (seconds > 0.0 ? seconds : 1.0), benchmark::Counter::kDefaults);
    state.SetItemsProcessed(total);
}

void pipeline_one_thread(benchmark::State& state) {
    run_throughput(state, false);
}
BENCHMARK(pipeline_one_thread)->UseManualTime()->Unit(benchmark::kMillisecond);

void pipeline_two_threads(benchmark::State& state) {
    run_throughput(state, true);
}
BENCHMARK(pipeline_two_threads)->UseManualTime()->Unit(benchmark::kMillisecond);

void on_demand_wakeup(benchmark::State& state) {
    atp::pipeline pipe;
    atp::group& producing = pipe.root().add_group("producing");
    atp::group& consuming = pipe.root().add_group("consuming");
    probe_source& src = producing.make<probe_source>("src");
    probe_sink& dst = consuming.make<probe_sink>("dst");
    producing.expose_output("out", "src.stamp");
    consuming.expose_input("in", "dst.stamp");
    pipe.root().connect("producing.out", "consuming.in");

    atp::pipeline_runner runner;
    runner.add_thread("producing", {atp::thread_mode::spinning});
    runner.add_thread("consuming", {atp::thread_mode::on_demand});
    runner.assign(producing, "producing");
    runner.assign(consuming, "consuming");
    runner.start(pipe);

    std::vector<std::int64_t> samples;
    samples.reserve(static_cast<std::size_t>(state.max_iterations));

    for (auto _ : state) {
        dst.latency_ns.store(-1, std::memory_order_release);
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        src.armed.store(true, std::memory_order_release);
        std::int64_t observed = -1;
        while ((observed = dst.latency_ns.load(std::memory_order_acquire)) < 0) {
            std::this_thread::yield();
        }
        samples.push_back(observed);
        state.SetIterationTime(static_cast<double>(observed) / 1e9);
    }
    runner.stop();

    std::ranges::sort(samples);
    const auto at = [&](double q) {
        return samples.empty() ? 0.0
                               : static_cast<double>(
                                     samples[static_cast<std::size_t>(static_cast<double>(samples.size() - 1) * q)]);
    };
    state.counters["p50_ns"] = at(0.5);
    state.counters["p99_ns"] = at(0.99);
    state.counters["max_ns"] = samples.empty() ? 0.0 : static_cast<double>(samples.back());
}
BENCHMARK(on_demand_wakeup)->UseManualTime()->Unit(benchmark::kMicrosecond)->Iterations(200);

}  // namespace
