// SPDX-License-Identifier: Apache-2.0
#include <any>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <typeindex>
#include <typeinfo>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <atp/io.hpp>

namespace {

struct test_inputs : atp::io::inputs {
    atp::io::input<int>& input1 = make<atp::io::input<int>>("input1");
    atp::io::input<std::string>& input2 = make<atp::io::input<std::string>>("input2");
};

struct test_outputs : atp::io::outputs {
    atp::io::output<int>& out1 = make<atp::io::output<int>>("out1");
    atp::io::output<std::string>& out2 = make<atp::io::output<std::string>>("out2");
};

template <std::size_t N>
struct big_payload {
    std::array<std::uint8_t, N> bytes{};
};

template <std::size_t N>
class first_byte_input : public atp::io::input<big_payload<N>> {
   public:
    using atp::io::input<big_payload<N>>::input;

    std::uint8_t first = 0;

   protected:
    void store(big_payload<N>&& value) override {
        first = value.bytes[0];
    }

    void store(const big_payload<N>& value) override {
        first = value.bytes[0];
    }
};

struct copy_counter {
    static inline int copies = 0;
    static inline int moves = 0;

    std::array<std::uint8_t, 64> bytes{};

    copy_counter() = default;
    copy_counter(const copy_counter& other) : bytes(other.bytes) {
        ++copies;
    }
    copy_counter(copy_counter&& other) noexcept : bytes(other.bytes) {
        ++moves;
    }
    copy_counter& operator=(const copy_counter& other) {
        bytes = other.bytes;
        ++copies;
        return *this;
    }
    copy_counter& operator=(copy_counter&& other) noexcept {
        bytes = other.bytes;
        ++moves;
        return *this;
    }
    ~copy_counter() = default;
};

}  // namespace

TEST(Input, MetadataCarriesNameAndType) {
    atp::io::input<int> in{"in_int"};
    EXPECT_EQ(in.name(), "in_int");
    EXPECT_EQ(in.type(), std::type_index(typeid(int)));
}

TEST(Input, EmptyStateThrowsOnGet) {
    atp::io::input<int> in{"in_int"};
    EXPECT_TRUE(in.empty());
    EXPECT_THROW((void)in.get(), std::runtime_error);
}

TEST(Input, AcceptsRvalue) {
    atp::io::input<int> in{"in_int"};
    in(42);
    ASSERT_FALSE(in.empty());
    EXPECT_EQ(in.get(), 42);
}

TEST(Input, AcceptsLvalueWithoutMoving) {
    atp::io::input<std::string> in{"in_str"};
    std::string hello = "Hello";
    in(hello);
    EXPECT_EQ(in.get(), "Hello");
    EXPECT_EQ(hello, "Hello");
}

TEST(Input, ResetClearsValue) {
    atp::io::input<int> in{"in_int"};
    in(42);
    in.reset();
    EXPECT_TRUE(in.empty());
}

TEST(Input, TakeRemovesValue) {
    atp::io::input<int> in{"in_int"};
    in(7);
    auto taken = in.take();
    ASSERT_TRUE(taken.has_value());
    EXPECT_EQ(*taken, 7);
    EXPECT_TRUE(in.empty());
    EXPECT_EQ(in.take(), std::nullopt);
}

TEST(Input, TakeOnEmptyReturnsNullopt) {
    atp::io::input<int> in{"in_int"};
    EXPECT_EQ(in.take(), std::nullopt);
}

TEST(InputDeliverProtocol, TypedInputAcceptsExactlyItsType) {
    atp::io::input<int> in{"in_int"};
    EXPECT_TRUE(in.accepts(typeid(int)));
    EXPECT_FALSE(in.accepts(typeid(double)));
    EXPECT_FALSE(in.accepts(typeid(std::any)));
}

TEST(InputDeliverProtocol, DeliverStoresValue) {
    atp::io::input<int> in{"in_int"};
    int v = 42;
    in.deliver(&v, atp::io::input_base::erased_of<int>());
    EXPECT_EQ(in.get(), 42);
}

TEST(InputDeliverProtocol, AnyInputAcceptsEverything) {
    atp::io::input<std::any> in{"in_any"};
    EXPECT_TRUE(in.accepts(typeid(int)));
    EXPECT_TRUE(in.accepts(typeid(std::string)));
    EXPECT_TRUE(in.accepts(typeid(std::any)));
}

TEST(InputDeliverProtocol, AnyInputBoxesDeliveredValue) {
    atp::io::input<std::any> in{"in_any"};
    int v = 42;
    in.deliver(&v, atp::io::input_base::erased_of<int>());
    EXPECT_EQ(std::any_cast<int>(in.get()), 42);
}

TEST(InputDeliverProtocol, AnyToAnyIsNotDoubleBoxed) {
    atp::io::input<std::any> in{"in_any"};
    std::any v = 42;
    in.deliver(&v, atp::io::input_base::erased_of<std::any>());
    EXPECT_EQ(in.get().type(), typeid(int));
    EXPECT_EQ(std::any_cast<int>(in.get()), 42);
}

TEST(InputDeliverProtocol, QueuedAnyInputAccumulatesMixedTypes) {
    atp::io::queued_input<std::any> q{"q_any"};
    int i = 1;
    std::string s = "two";
    q.deliver(&i, atp::io::input_base::erased_of<int>());
    q.deliver(&s, atp::io::input_base::erased_of<std::string>());
    EXPECT_EQ(q.size(), 2u);
    EXPECT_EQ(std::any_cast<int>(q.pop()), 1);
    EXPECT_EQ(std::any_cast<std::string>(q.pop()), "two");
}

namespace {

struct counting_notifier final : atp::io::notifier_base {
    int count = 0;
    void notify() noexcept override {
        ++count;
    }
};

}  // namespace

TEST(InputNotifier, DeliveryNotifies) {
    atp::io::output<int> out("out");
    atp::io::input<int> in("in");
    counting_notifier n;
    in.set_notifier(&n);
    out.connect(in);

    out(7);
    EXPECT_EQ(n.count, 1);
    EXPECT_EQ(in.get(), 7);

    in.set_notifier(nullptr);
    out(8);
    EXPECT_EQ(n.count, 1);
}

TEST(InputNotifier, DirectWriteDoesNotNotify) {
    atp::io::input<int> in("in");
    counting_notifier n;
    in.set_notifier(&n);
    in(3);
    EXPECT_EQ(n.count, 0);
}

TEST(OutputObservation, SafeOutputCountsWrites) {
    atp::io::output<int> out("out");
    atp::io::output_base& base = out;
    EXPECT_EQ(base.write_count(), 0u);

    out(41);
    out(42);

    EXPECT_EQ(base.write_count(), 2u);
}

TEST(OutputObservation, UnsafeOutputIsNotObservable) {
    atp::io::output<int> out("out", atp::io::unsafe);
    out(7);
    EXPECT_EQ(out.write_count(), 0u);
}

TEST(UnsafeInput, BehavesLikeInput) {
    atp::io::input<int> in{"in_int", atp::io::unsafe};
    in(7);
    EXPECT_EQ(in.get(), 7);
    EXPECT_EQ(in.take(), std::optional<int>(7));
    EXPECT_TRUE(in.empty());
    in(8);
    in.reset();
    EXPECT_TRUE(in.empty());
}

TEST(UnsafeQueuedInput, BehavesLikeQueuedInput) {
    atp::io::queued_input<int> in{"q_int", atp::io::drop_oldest(32), atp::io::unsafe};
    in(1);
    in(2);
    EXPECT_EQ(in.pop(), 1);
    EXPECT_EQ(in.pop(), 2);
    EXPECT_TRUE(in.empty());
}

TEST(QueuedInput, EmptyInitiallyAndPopThrows) {
    atp::io::queued_input<int> in{"q_int"};
    EXPECT_TRUE(in.empty());
    EXPECT_EQ(in.size(), 0u);
    EXPECT_THROW((void)in.pop(), std::runtime_error);
}

TEST(QueuedInput, PopsInFifoOrder) {
    atp::io::queued_input<int> in{"q_int"};
    in(1);
    in(2);
    in(3);
    EXPECT_EQ(in.size(), 3u);
    EXPECT_EQ(in.pop(), 1);
    EXPECT_EQ(in.pop(), 2);
    EXPECT_EQ(in.pop(), 3);
    EXPECT_TRUE(in.empty());
}

TEST(QueuedInput, AcceptsLvalueWithoutMoving) {
    atp::io::queued_input<std::string> in{"q_str"};
    std::string hello = "Hello";
    in(hello);
    EXPECT_EQ(in.pop(), "Hello");
    EXPECT_EQ(hello, "Hello");
}

TEST(QueuedInput, MetadataMatchesSignature) {
    atp::io::queued_input<int> in{"q_int"};
    EXPECT_EQ(in.name(), "q_int");
    EXPECT_EQ(in.type(), std::type_index(typeid(int)));
}

TEST(QueuedInput, ResetClearsQueue) {
    atp::io::queued_input<int> in{"q_int"};
    in(1);
    in(2);
    in.reset();
    EXPECT_TRUE(in.empty());
    EXPECT_THROW((void)in.pop(), std::runtime_error);
}

TEST(QueuedInput, TryPopReturnsNulloptWhenEmpty) {
    atp::io::queued_input<int> in{"q_int"};
    EXPECT_FALSE(in.try_pop().has_value());
    in(5);
    auto item = in.try_pop();
    ASSERT_TRUE(item.has_value());
    EXPECT_EQ(*item, 5);
    EXPECT_TRUE(in.empty());
}

TEST(QueuedInput, DrainTakesEverythingAtOnce) {
    atp::io::queued_input<int> in{"q_int"};
    in(1);
    in(2);
    in(3);
    auto items = in.drain();
    EXPECT_EQ(items.size(), 3u);
    EXPECT_TRUE(in.empty());
    EXPECT_EQ(items.front(), 1);
    EXPECT_EQ(items.back(), 3);
}

TEST(QueuedInput, TakePopsHead) {
    atp::io::queued_input<int> q{"q_int"};
    q(1);
    q(2);
    atp::io::input<int>& as_base = q;
    EXPECT_EQ(as_base.take(), std::optional<int>(1));
    EXPECT_EQ(q.size(), 1u);
}

TEST(Watcher, FiresHandlerOnFreshValue) {
    atp::io::input<int> in{"in_int"};
    atp::io::watcher w;
    int observed = 0;
    w.watch(in, [&](const int& v) { observed = v; });
    EXPECT_EQ(w.poll(), atp::io::work_status::idle);
    in(7);
    EXPECT_EQ(w.poll(), atp::io::work_status::busy);
    EXPECT_EQ(observed, 7);
    EXPECT_TRUE(in.empty());
    EXPECT_EQ(w.poll(), atp::io::work_status::idle);
}

TEST(Watcher, QueuedRuleDrainsPerElement) {
    atp::io::queued_input<int> q{"q_int"};
    atp::io::watcher w;
    std::vector<int> seen;
    w.watch(q, [&](const int& v) { seen.push_back(v); });
    q(1);
    q(2);
    q(3);
    EXPECT_EQ(w.poll(), atp::io::work_status::busy);
    EXPECT_EQ(seen, (std::vector<int>{1, 2, 3}));
    EXPECT_TRUE(q.empty());
    EXPECT_EQ(w.poll(), atp::io::work_status::idle);
}

TEST(Watcher, PropertyRuleFiresOnChange) {
    atp::io::property<int> limit("limit", 10);
    atp::io::watcher w;
    std::vector<int> seen;
    w.watch(limit, [&](const int& v) { seen.push_back(v); });

    EXPECT_EQ(w.poll(), atp::io::work_status::idle);
    limit(42);
    EXPECT_EQ(w.poll(), atp::io::work_status::busy);
    EXPECT_EQ(w.poll(), atp::io::work_status::idle);
    ASSERT_EQ(seen.size(), 1u);
    EXPECT_EQ(seen[0], 42);
}

TEST(Watcher, PollWithoutRulesIsIdle) {
    atp::io::watcher w;
    EXPECT_EQ(w.poll(), atp::io::work_status::idle);
}

TEST(Watcher, HandlerRunsOnPollingThread) {
    atp::io::input<int> in{"in_int"};
    atp::io::watcher w;
    std::thread::id handler_thread{};
    w.watch(in, [&](const int&) { handler_thread = std::this_thread::get_id(); });
    {
        std::jthread writer([&in] { in(7); });
    }
    EXPECT_EQ(w.poll(), atp::io::work_status::busy);
    EXPECT_EQ(handler_thread, std::this_thread::get_id());
}

TEST(QueuedInput, ConcurrentProducersLoseNothing) {
    constexpr int thread_count = 4;
    constexpr int per_thread = 1000;
    atp::io::queued_input<int> in{"q_int", atp::io::drop_oldest(thread_count * per_thread)};
    {
        std::vector<std::jthread> producers;
        producers.reserve(thread_count);
        for (int t = 0; t < thread_count; ++t) {
            producers.emplace_back([&in] {
                for (int i = 0; i < per_thread; ++i) {
                    in(i);
                }
            });
        }
    }
    EXPECT_EQ(in.size(), static_cast<std::size_t>(thread_count) * per_thread);
}

TEST(QueuedInput, ProducerAndConsumerRunConcurrently) {
    constexpr int count = 5000;
    atp::io::queued_input<int> in{"q_int", atp::io::drop_oldest(count)};
    long long sum = 0;
    int received = 0;
    {
        std::jthread producer([&in] {
            for (int i = 1; i <= count; ++i) {
                in(i);
            }
        });
        while (received < count) {
            if (auto item = in.try_pop()) {
                sum += *item;
                ++received;
            }
        }
    }
    EXPECT_EQ(sum, static_cast<long long>(count) * (count + 1) / 2);
}

TEST(InputsRegistry, TypedFieldAccess) {
    test_inputs ins;
    ins.input1(42);
    EXPECT_EQ(ins.input1.get(), 42);
}

TEST(InputsRegistry, AtByNameReturnsMetadata) {
    test_inputs ins;
    const atp::io::input_base& in = ins.at("input1");
    EXPECT_EQ(in.name(), "input1");
    EXPECT_EQ(in.type(), std::type_index(typeid(int)));
}

TEST(InputsRegistry, GetInputAliasesField) {
    test_inputs ins;
    ins.get<atp::io::input<int>>("input1")(100);
    EXPECT_EQ(ins.input1.get(), 100);
}

TEST(InputsRegistry, WrongTypeThrows) {
    test_inputs ins;
    EXPECT_THROW((void)ins.get<atp::io::input<std::string>>("input1"), std::runtime_error);
}

TEST(InputsRegistry, QueuedInputThroughRegistry) {
    atp::io::inputs ins;
    atp::io::queued_input<int>& q = ins.make<atp::io::queued_input<int>>("q");
    q(1);
    ins.get<atp::io::queued_input<int>>("q")(2);
    EXPECT_EQ(q.pop(), 1);
    EXPECT_EQ(q.pop(), 2);
}

TEST(InputsRegistry, UnsafeInputsThroughRegistry) {
    atp::io::inputs ins;
    atp::io::input<int>& fast = ins.make<atp::io::input<int>>("fast", atp::io::unsafe);
    atp::io::queued_input<int>& q =
        ins.make<atp::io::queued_input<int>>("q", atp::io::drop_oldest(32), atp::io::unsafe);
    fast(1);
    q(2);
    EXPECT_EQ(ins.get<atp::io::input<int>>("fast").get(), 1);
    EXPECT_EQ(ins.get<atp::io::queued_input<int>>("q").pop(), 2);
}

TEST(InputsRegistry, SafetyIsNotPartOfTheType) {
    atp::io::inputs ins;
    ins.make<atp::io::input<int>>("safe");
    ins.make<atp::io::input<int>>("fast", atp::io::unsafe);
    ins.get<atp::io::input<int>>("safe")(1);
    ins.get<atp::io::input<int>>("fast")(2);
    EXPECT_EQ(ins.get<atp::io::input<int>>("safe").get(), 1);
    EXPECT_EQ(ins.get<atp::io::input<int>>("fast").get(), 2);
}

TEST(InputsRegistry, KindMismatchThrows) {
    test_inputs ins;
    ins.make<atp::io::queued_input<int>>("q");
    EXPECT_THROW((void)ins.get<atp::io::queued_input<int>>("input1"), std::runtime_error);
    EXPECT_THROW((void)ins.get<atp::io::input<int>>("q"), std::runtime_error);
}

TEST(InputsRegistry, UnknownNameThrows) {
    test_inputs ins;
    EXPECT_THROW((void)ins.at("no_such_input"), std::runtime_error);
    EXPECT_THROW((void)ins.get<atp::io::input<int>>("no_such_input"), std::runtime_error);
}

TEST(InputsRegistry, FindDoesNotThrow) {
    test_inputs ins;
    atp::io::input_base* in = ins.find("input1");
    ASSERT_NE(in, nullptr);
    EXPECT_EQ(in->name(), "input1");
    EXPECT_EQ(ins.find("no_such_input"), nullptr);
}

TEST(InputsRegistry, ListEnumeratesAll) {
    test_inputs ins;
    EXPECT_EQ(ins.list().size(), 2u);
}

TEST(InputsRegistry, DuplicateNameThrowsOnConstruction) {
    struct duplicate_inputs : atp::io::inputs {
        atp::io::input<int>& a = make<atp::io::input<int>>("same");
        atp::io::input<int>& b = make<atp::io::input<int>>("same");
    };
    EXPECT_THROW((void)duplicate_inputs{}, std::runtime_error);
}

TEST(InputsRegistry, DynamicInputCanBeRemoved) {
    test_inputs ins;
    atp::io::input<int>& extra = ins.make<atp::io::input<int>>("extra");
    extra(5);
    EXPECT_EQ(ins.list().size(), 3u);
    EXPECT_EQ(ins.get<atp::io::input<int>>("extra").get(), 5);

    EXPECT_TRUE(ins.remove("extra"));
    EXPECT_EQ(ins.list().size(), 2u);
    EXPECT_THROW((void)ins.at("extra"), std::runtime_error);
    EXPECT_FALSE(ins.remove("extra"));
}

TEST(InputsRegistry, MoveKeepsPortsAlive) {
    struct movable_inputs : atp::io::inputs {
        atp::io::input<int>& number = make<atp::io::input<int>>("number");
    };
    movable_inputs src;
    src.number(41);
    movable_inputs dst{std::move(src)};
    EXPECT_EQ(dst.find("number"), static_cast<atp::io::input_base*>(&dst.number));
    EXPECT_EQ(dst.number.get(), 41);
}

TEST(Output, MetadataCarriesNameAndType) {
    atp::io::output<int> out{"out_int"};
    EXPECT_EQ(out.name(), "out_int");
    EXPECT_EQ(out.type(), std::type_index(typeid(int)));
}

TEST(Output, WriteWithoutTargetsOnlyCounts) {
    atp::io::output<int> out{"out_int"};
    out(42);
    EXPECT_EQ(out.write_count(), 1u);
    EXPECT_EQ(out.connections(), 0u);
}

TEST(Output, AcceptsLvalueWithoutMoving) {
    atp::io::output<std::string> out{"out_str"};
    atp::io::input<std::string> in{"in_str"};
    out.connect(in);
    std::string hello = "Hello";
    out(hello);
    EXPECT_EQ(in.get(), "Hello");
    EXPECT_EQ(hello, "Hello");
}

TEST(Output, DeliversToAllConnectedInputs) {
    atp::io::output<int> out{"out_int"};
    atp::io::input<int> a{"a"};
    atp::io::input<int> b{"b"};
    out.connect(a);
    out.connect(b);
    EXPECT_EQ(out.connections(), 2u);
    out(7);
    EXPECT_EQ(a.get(), 7);
    EXPECT_EQ(b.get(), 7);
}

TEST(Output, DeliversToQueuedInput) {
    atp::io::output<int> out{"out_int"};
    atp::io::queued_input<int> q{"q"};
    out.connect(q);
    out(1);
    out(2);
    EXPECT_EQ(q.pop(), 1);
    EXPECT_EQ(q.pop(), 2);
}

TEST(Output, ResetZeroesTheWriteCounter) {
    atp::io::output<int> out{"out_int"};
    atp::io::input<int> in{"in"};
    out.connect(in);
    out(7);
    EXPECT_EQ(out.write_count(), 1u);

    out.reset();

    EXPECT_EQ(out.write_count(), 0u);
    EXPECT_EQ(out.connections(), 1u);
    out(9);
    EXPECT_EQ(in.get(), 9);
    EXPECT_EQ(out.write_count(), 1u);
}

TEST(Output, DuplicateConnectThrows) {
    atp::io::output<int> out{"out_int"};
    atp::io::input<int> in{"in"};
    out.connect(in);
    EXPECT_THROW(out.connect(in), std::runtime_error);
    EXPECT_EQ(out.connections(), 1u);
}

TEST(Output, DisconnectStopsDelivery) {
    atp::io::output<int> out{"out_int"};
    atp::io::input<int> in{"in"};
    out.connect(in);
    out(1);
    EXPECT_TRUE(out.disconnect(in));
    EXPECT_FALSE(out.disconnect(in));
    out(2);
    EXPECT_EQ(in.get(), 1);
}

TEST(Output, DisconnectAllDropsEveryConnection) {
    atp::io::output<int> out{"out_int"};
    atp::io::input<int> a{"a"};
    atp::io::input<int> b{"b"};
    out.connect(a);
    out.connect(b);
    out.disconnect_all();
    EXPECT_EQ(out.connections(), 0u);
    out(7);
    EXPECT_TRUE(a.empty());
    EXPECT_TRUE(b.empty());
}

TEST(Output, ALateSubscriberGetsNothingUntilTheNextWrite) {
    atp::io::output<int> out{"out_int"};
    atp::io::input<int> in{"in"};
    out(42);

    out.connect(in);

    EXPECT_TRUE(in.empty());
    out(43);
    EXPECT_EQ(in.get(), 43);
}

TEST(Output, TypeErasedConnectChecksCompatibility) {
    test_outputs outs;
    test_inputs ins;
    atp::io::output_base& out = outs.at("out1");
    out.connect(ins.at("input1"));
    outs.out1(5);
    EXPECT_EQ(ins.input1.get(), 5);
    EXPECT_THROW(out.connect(ins.at("input2")), std::runtime_error);
}

TEST(Output, TypeErasedConnectAcceptsQueuedInput) {
    atp::io::inputs ins;
    atp::io::queued_input<int>& q = ins.make<atp::io::queued_input<int>>("q");
    atp::io::output<int> out{"out_int"};
    static_cast<atp::io::output_base&>(out).connect(ins.at("q"));
    out(1);
    EXPECT_EQ(q.pop(), 1);
}

TEST(Output, TypeErasedConnectAcceptsAnyInput) {
    atp::io::output<int> out{"out_int"};
    atp::io::input<std::any> any_in{"any_in"};
    static_cast<atp::io::output_base&>(out).connect(any_in);
    out(7);
    EXPECT_EQ(std::any_cast<int>(any_in.get()), 7);
}

TEST(Output, TypedConnectAcceptsAnyInput) {
    atp::io::output<std::string> out{"out_str"};
    atp::io::input<std::any> any_in{"any_in"};
    out.connect(any_in);
    out(std::string("hello"));
    EXPECT_EQ(std::any_cast<std::string>(any_in.get()), "hello");
}

TEST(Output, QueuedAnyInputAccumulatesFromTypedOutput) {
    atp::io::output<int> out{"out_int"};
    atp::io::queued_input<std::any> q{"q_any"};
    out.connect(q);
    out(1);
    out(2);
    EXPECT_EQ(q.size(), 2u);
    EXPECT_EQ(std::any_cast<int>(q.pop()), 1);
    EXPECT_EQ(std::any_cast<int>(q.pop()), 2);
}

TEST(Output, AnyOutputToAnyInputNoDoubleBoxing) {
    atp::io::output<std::any> out{"out_any"};
    atp::io::input<std::any> in{"in_any"};
    out.connect(in);
    out(std::any(42));
    EXPECT_EQ(in.get().type(), typeid(int));
    EXPECT_EQ(std::any_cast<int>(in.get()), 42);
}

TEST(Output, AnyOutputTypedConnectHasNoAmbiguity) {
    atp::io::output<std::any> out{"out_any"};
    atp::io::input<std::any> in{"in_any"};
    out.connect(in);
    out(std::string("x"));
    EXPECT_EQ(std::any_cast<std::string>(in.get()), "x");
}

TEST(Output, IncompatibleInputStillRejected) {
    atp::io::output<int> out{"out_int"};
    atp::io::input<double> in{"in_double"};
    EXPECT_THROW(static_cast<atp::io::output_base&>(out).connect(in), std::runtime_error);
    EXPECT_EQ(out.connections(), 0u);
}

TEST(Output, DuplicateAnyConnectThrowsAcrossPaths) {
    atp::io::output<int> out{"out_int"};
    atp::io::input<std::any> any_in{"any_in"};
    out.connect(any_in);
    EXPECT_THROW(static_cast<atp::io::output_base&>(out).connect(any_in), std::runtime_error);
    EXPECT_EQ(out.connections(), 1u);
}

TEST(Output, DisconnectAnyInputStopsDelivery) {
    atp::io::output<int> out{"out_int"};
    atp::io::input<std::any> any_in{"any_in"};
    out.connect(any_in);
    out(1);
    EXPECT_TRUE(out.disconnect(any_in));
    out(2);
    EXPECT_EQ(std::any_cast<int>(any_in.get()), 1);
    EXPECT_EQ(out.connections(), 0u);
}

TEST(Output, ConcurrentWritersLoseNothingInQueuedTarget) {
    constexpr int thread_count = 4;
    constexpr int per_thread = 1000;
    atp::io::output<int> out{"out_int"};
    atp::io::queued_input<int> q{"q", atp::io::drop_oldest(thread_count * per_thread)};
    out.connect(q);
    {
        std::vector<std::jthread> writers;
        writers.reserve(thread_count);
        for (int t = 0; t < thread_count; ++t) {
            writers.emplace_back([&out] {
                for (int i = 0; i < per_thread; ++i) {
                    out(i);
                }
            });
        }
    }
    EXPECT_EQ(q.size(), static_cast<std::size_t>(thread_count) * per_thread);
    EXPECT_EQ(out.write_count(), static_cast<std::uint64_t>(thread_count) * per_thread);
}

TEST(OutputWritePath, WritingCostsOneCopyPerSubscriber) {
    atp::io::output<copy_counter> out("out");
    atp::io::input<copy_counter> in("in");
    out.connect(in);

    const copy_counter value;
    copy_counter::copies = 0;
    copy_counter::moves = 0;
    out(value);

    EXPECT_EQ(copy_counter::copies, 1);
    EXPECT_EQ(copy_counter::moves, 1);
}

TEST(OutputWritePath, HalfAMegabyteTravelsWithoutOverflowingTheStack) {
    using payload = big_payload<512UL * 1024UL>;
    auto out = std::make_unique<atp::io::output<payload>>("out");
    auto in = std::make_unique<first_byte_input<512UL * 1024UL>>("in");
    auto value = std::make_unique<payload>();
    value->bytes[0] = 5;
    out->connect(*in);

    (*out)(*value);

    EXPECT_EQ(in->first, 5);
}

TEST(OutputWritePath, RvalueWriteMovesIntoTheOnlySubscriber) {
    atp::io::output<copy_counter> out("out");
    atp::io::input<copy_counter> in("in");
    out.connect(in);

    copy_counter value;
    copy_counter::copies = 0;
    copy_counter::moves = 0;
    out(std::move(value));

    EXPECT_EQ(copy_counter::copies, 0);
    EXPECT_EQ(copy_counter::moves, 1);
}

TEST(OutputWritePath, RvalueWriteCopiesToEveryoneButTheLast) {
    atp::io::output<copy_counter> out("out");
    std::vector<std::unique_ptr<atp::io::input<copy_counter>>> inputs;
    for (int i = 0; i < 4; ++i) {
        inputs.push_back(std::make_unique<atp::io::input<copy_counter>>("in" + std::to_string(i)));
        out.connect(*inputs.back());
    }

    copy_counter value;
    copy_counter::copies = 0;
    copy_counter::moves = 0;
    out(std::move(value));

    EXPECT_EQ(copy_counter::copies, 3);
    EXPECT_EQ(copy_counter::moves, 4);
    for (auto& in : inputs) {
        (void)out.disconnect(*in);
    }
}

TEST(OutputWritePath, AnyInputStillReceivesAMovedWrite) {
    atp::io::output<int> out("out");
    atp::io::input<std::any> any_in("any");
    out.connect(any_in);

    out(42);

    EXPECT_EQ(std::any_cast<int>(any_in.get()), 42);
}

TEST(InputStorePath, QueuedInputKeepsBothHalvesOfTheExtensionPoint) {
    atp::io::output<int> out("out");
    atp::io::queued_input<int> queued("q");
    out.connect(queued);

    out(1);
    const int lvalue = 2;
    out(lvalue);
    queued(3);

    ASSERT_EQ(queued.size(), 3u);
    EXPECT_EQ(queued.pop(), 1);
    EXPECT_EQ(queued.pop(), 2);
    EXPECT_EQ(queued.pop(), 3);
}

TEST(InputStorePath, FourMegabytesTravelWithoutOverflowingTheStack) {
    using payload = big_payload<4UL * 1024UL * 1024UL>;
    auto out = std::make_unique<atp::io::output<payload>>("out");
    auto in = std::make_unique<first_byte_input<4UL * 1024UL * 1024UL>>("in");
    auto value = std::make_unique<payload>();
    value->bytes[0] = 9;
    out->connect(*in);

    (*out)(*value);

    EXPECT_EQ(in->first, 9);
}

TEST(UnsafeOutput, BehavesLikeOutput) {
    atp::io::output<int> out{"out_int", atp::io::unsafe};
    atp::io::input<int> in{"in", atp::io::unsafe};
    out.connect(in);
    out(7);
    EXPECT_EQ(in.get(), 7);
    out.reset();
    EXPECT_EQ(out.connections(), 1u);
    out(8);
    EXPECT_EQ(in.get(), 8);
}

TEST(OutputsRegistry, TypedFieldAccess) {
    test_outputs outs;
    atp::io::input<int> in{"in"};
    outs.out1.connect(in);
    outs.out1(42);
    EXPECT_EQ(in.get(), 42);
    EXPECT_EQ(&outs.get<atp::io::output<int>>("out1"), &outs.out1);
    EXPECT_EQ(outs.get<atp::io::output<int>>("out1").write_count(), 1u);
}

TEST(OutputsRegistry, AtByNameReturnsMetadata) {
    test_outputs outs;
    const atp::io::output_base& out = outs.at("out1");
    EXPECT_EQ(out.name(), "out1");
    EXPECT_EQ(out.type(), std::type_index(typeid(int)));
}

TEST(OutputsRegistry, WrongTypeThrows) {
    test_outputs outs;
    EXPECT_THROW((void)outs.get<atp::io::output<std::string>>("out1"), std::runtime_error);
}

TEST(OutputsRegistry, UnknownNameThrows) {
    test_outputs outs;
    EXPECT_THROW((void)outs.at("no_such_output"), std::runtime_error);
    EXPECT_THROW((void)outs.get<atp::io::output<int>>("no_such_output"), std::runtime_error);
}

TEST(OutputsRegistry, FindDoesNotThrow) {
    test_outputs outs;
    atp::io::output_base* out = outs.find("out1");
    ASSERT_NE(out, nullptr);
    EXPECT_EQ(out->name(), "out1");
    EXPECT_EQ(outs.find("no_such_output"), nullptr);
}

TEST(OutputsRegistry, ListEnumeratesAll) {
    test_outputs outs;
    EXPECT_EQ(outs.list().size(), 2u);
}

TEST(OutputsRegistry, DuplicateNameThrowsOnConstruction) {
    struct duplicate_outputs : atp::io::outputs {
        atp::io::output<int>& a = make<atp::io::output<int>>("same");
        atp::io::output<int>& b = make<atp::io::output<int>>("same");
    };
    EXPECT_THROW((void)duplicate_outputs{}, std::runtime_error);
}

TEST(OutputsRegistry, DynamicOutputCanBeRemoved) {
    test_outputs outs;
    atp::io::output<int>& extra = outs.make<atp::io::output<int>>("extra");
    extra(5);
    EXPECT_EQ(outs.list().size(), 3u);
    EXPECT_EQ(outs.get<atp::io::output<int>>("extra").write_count(), 1u);

    EXPECT_TRUE(outs.remove("extra"));
    EXPECT_EQ(outs.list().size(), 2u);
    EXPECT_THROW((void)outs.at("extra"), std::runtime_error);
    EXPECT_FALSE(outs.remove("extra"));
}

TEST(IoBase, ThreadSafeReflectsConstructionTag) {
    atp::io::input<int> guarded("guarded");
    atp::io::input<int> bare("bare", atp::io::unsafe);
    atp::io::output<int> guarded_out("out");
    EXPECT_TRUE(guarded.thread_safe());
    EXPECT_FALSE(bare.thread_safe());
    EXPECT_TRUE(guarded_out.thread_safe());
}

TEST(IoRegistry, AliasSharesForeignPort) {
    atp::io::input<int> real{"real"};
    atp::io::inputs regs;
    regs.alias("mirror", real);
    EXPECT_EQ(regs.find("mirror"), &real);
    real(7);
    EXPECT_EQ(regs.get<atp::io::input<int>>("mirror").get(), 7);
}

TEST(IoRegistry, AliasRejectsDuplicateName) {
    atp::io::inputs regs;
    (void)regs.make<atp::io::input<int>>("port");
    atp::io::input<int> foreign{"foreign"};
    EXPECT_THROW(regs.alias("port", foreign), std::runtime_error);
}

TEST(IoRegistry, OwnedSkipsAliases) {
    atp::io::inputs regs;
    auto& own = regs.make<atp::io::input<int>>("own");
    atp::io::input<int> foreign{"foreign"};
    regs.alias("mirror", foreign);
    auto owned = regs.owned();
    ASSERT_EQ(owned.size(), 1u);
    EXPECT_EQ(owned.front(), &own);
    EXPECT_EQ(regs.list().size(), 2u);
}

TEST(IoRegistry, DestructionLeavesAliasedPortAlive) {
    atp::io::input<int> foreign{"foreign"};
    {
        atp::io::inputs regs;
        regs.alias("mirror", foreign);
    }
    foreign(5);
    EXPECT_EQ(foreign.get(), 5);
}

TEST(InputStats, CountsReceivedAndOverwrites) {
    atp::io::input<int> in{"state"};
    in(1);
    in(2);
    in(3);
    const atp::io::input_stats s = in.stats();
    EXPECT_EQ(s.received, 3u);
    EXPECT_EQ(s.discarded, 2u);
    EXPECT_EQ(s.pending, 1u);
    EXPECT_EQ(s.peak_pending, 1u);
    EXPECT_EQ(s.capacity, 1u);
}

TEST(InputStats, TakingBetweenWritesLosesNothing) {
    atp::io::input<int> in{"state"};
    in(1);
    EXPECT_EQ(in.take().value(), 1);
    in(2);
    EXPECT_EQ(in.take().value(), 2);
    const atp::io::input_stats s = in.stats();
    EXPECT_EQ(s.received, 2u);
    EXPECT_EQ(s.discarded, 0u);
    EXPECT_EQ(s.pending, 0u);
}

TEST(InputStats, UnsafeInstanceIsUnobservable) {
    atp::io::input<int> in{"state", atp::io::unsafe};
    in(1);
    in(2);
    const atp::io::input_stats s = in.stats();
    EXPECT_EQ(s.received, 0u);
    EXPECT_EQ(s.discarded, 0u);
    EXPECT_EQ(s.pending, 0u);
    EXPECT_EQ(s.peak_pending, 0u);
    EXPECT_EQ(s.capacity, 0u);
}

TEST(InputStats, ResetClearsCounters) {
    atp::io::input<int> in{"state"};
    in(1);
    in(2);
    in.reset();
    const atp::io::input_stats s = in.stats();
    EXPECT_EQ(s.received, 0u);
    EXPECT_EQ(s.discarded, 0u);
    EXPECT_EQ(s.pending, 0u);
}

TEST(QueuedInputCapacity, DefaultsToThirtyTwoDroppingOldest) {
    atp::io::queued_input<int> in{"q"};
    EXPECT_EQ(in.capacity(), 32u);
    EXPECT_EQ(in.policy(), atp::io::overflow_policy::drop_oldest);
}

TEST(QueuedInputCapacity, ZeroCapacityIsRejected) {
    EXPECT_THROW((atp::io::queued_input<int>{"q", atp::io::drop_oldest(0)}), std::invalid_argument);
    EXPECT_THROW((atp::io::queued_input<int>{"q", atp::io::drop_incoming(0)}), std::invalid_argument);
}

TEST(QueuedInputCapacity, DropOldestKeepsTheLastValues) {
    atp::io::queued_input<int> in{"q", atp::io::drop_oldest(3)};
    for (int i = 1; i <= 5; ++i) {
        in(i);
    }
    EXPECT_EQ(in.size(), 3u);
    EXPECT_EQ(in.pop(), 3);
    EXPECT_EQ(in.pop(), 4);
    EXPECT_EQ(in.pop(), 5);
}

TEST(QueuedInputCapacity, DropIncomingKeepsTheFirstValues) {
    atp::io::queued_input<int> in{"q", atp::io::drop_incoming(3)};
    for (int i = 1; i <= 5; ++i) {
        in(i);
    }
    EXPECT_EQ(in.size(), 3u);
    EXPECT_EQ(in.pop(), 1);
    EXPECT_EQ(in.pop(), 2);
    EXPECT_EQ(in.pop(), 3);
}

TEST(QueuedInputCapacity, BothPoliciesCountTheirLosses) {
    atp::io::queued_input<int> oldest{"o", atp::io::drop_oldest(2)};
    atp::io::queued_input<int> incoming{"i", atp::io::drop_incoming(2)};
    for (int i = 1; i <= 5; ++i) {
        oldest(i);
        incoming(i);
    }
    EXPECT_EQ(oldest.stats().discarded, 3u);
    EXPECT_EQ(incoming.stats().discarded, 3u);
    EXPECT_EQ(oldest.stats().received, 5u);
    EXPECT_EQ(incoming.stats().received, 5u);
}

TEST(QueuedInputCapacity, StatsReportDepthAndDeclaredCapacity) {
    atp::io::queued_input<int> in{"q", atp::io::drop_oldest(4)};
    in(1);
    in(2);
    const atp::io::input_stats s = in.stats();
    EXPECT_EQ(s.pending, 2u);
    EXPECT_EQ(s.capacity, 4u);
}

TEST(QueuedInputCapacity, PeakSurvivesTheQueueEmptying) {
    atp::io::queued_input<int> in{"q", atp::io::drop_oldest(8)};
    in(1);
    in(2);
    in(3);
    EXPECT_EQ(in.drain().size(), 3u);
    const atp::io::input_stats s = in.stats();
    EXPECT_EQ(s.pending, 0u);
    EXPECT_EQ(s.peak_pending, 3u);
}

TEST(QueuedInputCapacity, MovedInValueIsDiscardedWhenIncomingLoses) {
    atp::io::queued_input<std::string> in{"q", atp::io::drop_incoming(1)};
    in(std::string("first"));
    in(std::string("second"));
    EXPECT_EQ(in.size(), 1u);
    EXPECT_EQ(in.pop(), "first");
    EXPECT_EQ(in.stats().discarded, 1u);
}

TEST(InputStats, CountsThroughAnHeirThatKeepsNoStorage) {
    first_byte_input<8> in{"first"};
    big_payload<8> payload;
    payload.bytes[0] = 7;
    in(payload);
    in(payload);
    const atp::io::input_stats s = in.stats();
    EXPECT_EQ(s.received, 2u);
    EXPECT_EQ(in.first, 7);
}

TEST(InputsRegistry, KeepsDeclarationOrder) {
    struct six : atp::io::inputs {
        atp::io::input<int>& zulu = make<atp::io::input<int>>("zulu");
        atp::io::input<int>& yankee = make<atp::io::input<int>>("yankee");
        atp::io::input<int>& xray = make<atp::io::input<int>>("xray");
        atp::io::input<int>& whiskey = make<atp::io::input<int>>("whiskey");
        atp::io::input<int>& victor = make<atp::io::input<int>>("victor");
        atp::io::input<int>& uniform = make<atp::io::input<int>>("uniform");
    };
    const six ins;
    const std::vector<std::string> expected = {"zulu", "yankee", "xray", "whiskey", "victor", "uniform"};

    std::vector<std::string> from_entries;
    for (const auto& e : ins.entries()) {
        from_entries.push_back(e.first);
    }
    EXPECT_EQ(from_entries, expected);

    std::vector<std::string> from_list;
    for (const atp::io::input_base* port : ins.list()) {
        from_list.emplace_back(port->name());
    }
    EXPECT_EQ(from_list, expected);

    std::vector<std::string> from_owned;
    for (const atp::io::input_base* port : ins.owned()) {
        from_owned.emplace_back(port->name());
    }
    EXPECT_EQ(from_owned, expected);
}

TEST(InputsRegistry, RemoveKeepsTheOrderOfTheRest) {
    struct three : atp::io::inputs {
        atp::io::input<int>& gamma = make<atp::io::input<int>>("gamma");
        atp::io::input<int>& beta = make<atp::io::input<int>>("beta");
        atp::io::input<int>& alpha = make<atp::io::input<int>>("alpha");
    };
    three ins;
    ASSERT_TRUE(ins.remove("beta"));

    std::vector<std::string> names;
    for (const auto& e : ins.entries()) {
        names.push_back(e.first);
    }
    EXPECT_EQ(names, (std::vector<std::string>{"gamma", "alpha"}));
}

TEST(InputsRegistry, ShortAndExplicitMakeAgree) {
    struct both : atp::io::inputs {
        atp::io::input<int>& plain = make<int>("plain");
        atp::io::queued_input<int>& queued = make<atp::io::queued_input<int>>("queued", atp::io::drop_oldest(8));
        atp::io::input<int>& spelled_out = make<atp::io::input<int>>("spelled_out");
    };
    both ins;
    EXPECT_EQ(ins.plain.type(), std::type_index(typeid(int)));
    EXPECT_EQ(ins.queued.type(), std::type_index(typeid(int)));
    EXPECT_EQ(ins.spelled_out.type(), std::type_index(typeid(int)));
    EXPECT_EQ(ins.list().size(), 3u);
}

TEST(PropertiesRegistry, DeducesTypeFromTheDefault) {
    struct deduced : atp::io::properties {
        atp::io::property<double>& gain = make("gain", 0.5);
        atp::io::property<int>& limit = make<int>("limit", 10);
        atp::io::property<std::string>& file = make<std::string>("file", "");
    };
    deduced props;
    EXPECT_EQ(props.gain.get(), 0.5);
    EXPECT_EQ(props.limit.get(), 10);
    EXPECT_EQ(props.file.get(), "");
}
