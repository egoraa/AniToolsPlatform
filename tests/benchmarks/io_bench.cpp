// SPDX-License-Identifier: Apache-2.0
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>

#include <atp/io/input.hpp>
#include <atp/io/output.hpp>
#include <atp/io/property.hpp>
#include <atp/io/queued_input.hpp>

namespace {

template <std::size_t N>
struct payload_of {
    std::array<std::uint8_t, N> bytes{};
};

void deliver_to_subscribers(benchmark::State& state) {
    const auto count = static_cast<std::size_t>(state.range(0));
    atp::io::output<int> out("out");
    std::vector<std::unique_ptr<atp::io::input<int>>> inputs;
    inputs.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        inputs.push_back(std::make_unique<atp::io::input<int>>("in" + std::to_string(i)));
        out.connect(*inputs.back());
    }
    int value = 0;
    for (auto _ : state) {
        out(value++);
        benchmark::ClobberMemory();
    }
    for (auto& in : inputs) {
        (void)out.disconnect(*in);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(count));
}
BENCHMARK(deliver_to_subscribers)->Arg(1)->Arg(4)->Arg(16);

void deliver_safe(benchmark::State& state) {
    atp::io::output<int> out("out", atp::io::safe);
    atp::io::input<int> in("in", atp::io::safe);
    out.connect(in);
    int value = 0;
    for (auto _ : state) {
        out(value++);
        benchmark::ClobberMemory();
    }
    (void)out.disconnect(in);
}
BENCHMARK(deliver_safe);

void deliver_unsafe(benchmark::State& state) {
    atp::io::output<int> out("out", atp::io::unsafe);
    atp::io::input<int> in("in", atp::io::unsafe);
    out.connect(in);
    int value = 0;
    for (auto _ : state) {
        out(value++);
        benchmark::ClobberMemory();
    }
    (void)out.disconnect(in);
}
BENCHMARK(deliver_unsafe);

void deliver_int(benchmark::State& state) {
    atp::io::output<int> out("out");
    atp::io::input<int> in("in");
    out.connect(in);
    int value = 0;
    for (auto _ : state) {
        out(value++);
        benchmark::ClobberMemory();
    }
    (void)out.disconnect(in);
    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(sizeof(int)));
}
BENCHMARK(deliver_int);

template <std::size_t N>
void deliver_payload(benchmark::State& state) {
    using payload = payload_of<N>;
    auto out = std::make_unique<atp::io::output<payload>>("out");
    auto in = std::make_unique<atp::io::input<payload>>("in");
    auto value = std::make_unique<payload>();
    out->connect(*in);
    for (auto _ : state) {
        (*out)(*value);
        benchmark::ClobberMemory();
    }
    (void)out->disconnect(*in);
    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(sizeof(payload)));
}
BENCHMARK(deliver_payload<1024UL>)->Name("deliver_payload/1KiB");
BENCHMARK(deliver_payload<64UL * 1024UL>)->Name("deliver_payload/64KiB");
BENCHMARK(deliver_payload<256UL * 1024UL>)->Name("deliver_payload/256KiB");

template <std::size_t N>
void deliver_payload_fanout(benchmark::State& state) {
    using payload = payload_of<N>;
    const auto count = static_cast<std::size_t>(state.range(0));
    auto out = std::make_unique<atp::io::output<payload>>("out");
    std::vector<std::unique_ptr<atp::io::input<payload>>> inputs;
    inputs.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        inputs.push_back(std::make_unique<atp::io::input<payload>>("in" + std::to_string(i)));
        out->connect(*inputs.back());
    }
    auto value = std::make_unique<payload>();
    for (auto _ : state) {
        (*out)(*value);
        benchmark::ClobberMemory();
    }
    for (auto& in : inputs) {
        (void)out->disconnect(*in);
    }
}
BENCHMARK(deliver_payload_fanout<1024UL>)->Name("fanout/1KiB")->Arg(0)->Arg(1)->Arg(2)->Arg(4);
BENCHMARK(deliver_payload_fanout<64UL * 1024UL>)->Name("fanout/64KiB")->Arg(0)->Arg(1)->Arg(2)->Arg(4);
BENCHMARK(deliver_payload_fanout<256UL * 1024UL>)->Name("fanout/256KiB")->Arg(0)->Arg(1)->Arg(2)->Arg(4);

struct movable_payload {
    std::vector<std::uint8_t> bytes;
};

template <std::size_t N>
void deliver_movable(benchmark::State& state) {
    const bool by_move = state.range(0) != 0;
    auto out = std::make_unique<atp::io::output<movable_payload>>("out");
    auto in = std::make_unique<atp::io::input<movable_payload>>("in");
    out->connect(*in);
    for (auto _ : state) {
        movable_payload fresh{std::vector<std::uint8_t>(N)};
        if (by_move) {
            (*out)(std::move(fresh));
        } else {
            (*out)(fresh);
        }
        benchmark::ClobberMemory();
    }
    (void)out->disconnect(*in);
}
BENCHMARK(deliver_movable<1024UL>)->Name("movable/1KiB")->Arg(0)->Arg(1);
BENCHMARK(deliver_movable<64UL * 1024UL>)->Name("movable/64KiB")->Arg(0)->Arg(1);
BENCHMARK(deliver_movable<256UL * 1024UL>)->Name("movable/256KiB")->Arg(0)->Arg(1);

void read_state_input(benchmark::State& state) {
    atp::io::output<int> out("out");
    atp::io::input<int> in("in");
    out.connect(in);
    out(7);
    for (auto _ : state) {
        benchmark::DoNotOptimize(in.get());
    }
    (void)out.disconnect(in);
}
BENCHMARK(read_state_input);

void read_queued_pop(benchmark::State& state) {
    atp::io::output<int> out("out");
    atp::io::queued_input<int> in("in");
    out.connect(in);
    int value = 0;
    for (auto _ : state) {
        out(value++);
        benchmark::DoNotOptimize(in.try_pop());
    }
    (void)out.disconnect(in);
}
BENCHMARK(read_queued_pop);

void read_queued_drain(benchmark::State& state) {
    const auto batch = static_cast<std::size_t>(state.range(0));
    atp::io::output<int> out("out");
    atp::io::queued_input<int> in("in", atp::io::drop_oldest(1024));
    out.connect(in);
    int value = 0;
    for (auto _ : state) {
        state.PauseTiming();
        for (std::size_t i = 0; i < batch; ++i) {
            out(value++);
        }
        state.ResumeTiming();
        benchmark::DoNotOptimize(in.drain());
    }
    (void)out.disconnect(in);
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(batch));
}
BENCHMARK(read_queued_drain)->Arg(16)->Arg(256);

void read_queued_pop_batch(benchmark::State& state) {
    const auto batch = static_cast<std::size_t>(state.range(0));
    atp::io::output<int> out("out");
    atp::io::queued_input<int> in("in", atp::io::drop_oldest(1024));
    out.connect(in);
    int value = 0;
    for (auto _ : state) {
        state.PauseTiming();
        for (std::size_t i = 0; i < batch; ++i) {
            out(value++);
        }
        state.ResumeTiming();
        while (const auto item = in.try_pop()) {
            benchmark::DoNotOptimize(item);
        }
    }
    (void)out.disconnect(in);
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(batch));
}
BENCHMARK(read_queued_pop_batch)->Arg(16)->Arg(256);

void property_get(benchmark::State& state) {
    atp::io::property<int> p("gain", 3);
    for (auto _ : state) {
        benchmark::DoNotOptimize(p.get());
    }
}
BENCHMARK(property_get);

void property_set(benchmark::State& state) {
    atp::io::property<int> p("gain", 3);
    int value = 0;
    for (auto _ : state) {
        p(value++);
        benchmark::ClobberMemory();
    }
}
BENCHMARK(property_set);

}  // namespace
