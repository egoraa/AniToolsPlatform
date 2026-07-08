#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <typeindex>
#include <typeinfo>
#include <vector>

#include <gtest/gtest.h>

#include "platform/io.hpp"

namespace {

    struct test_inputs : atp::io::inputs {
        atp::io::input<int>& input1 = make<atp::io::input<int>>("input1");
        atp::io::input<std::string>& input2 = make<atp::io::input<std::string>>("input2");
    };

} // namespace

TEST(Input, MetadataCarriesNameAndType) {
    atp::io::input<int> in{"in_int"};
    EXPECT_EQ(in.name(), "in_int");
    EXPECT_EQ(in.type(), std::type_index(typeid(std::tuple<int>)));
}

TEST(Input, EmptyStateThrowsOnGet) {
    atp::io::input<int> in{"in_int"};
    EXPECT_FALSE(in.has_value());
    EXPECT_THROW((void)in.get(), std::runtime_error);
}

TEST(Input, AcceptsRvalue) {
    atp::io::input<int> in{"in_int"};
    in(42);
    ASSERT_TRUE(in.has_value());
    EXPECT_EQ(std::get<0>(in.get()), 42);
}

TEST(Input, AcceptsLvalueWithoutMoving) {
    atp::io::input<std::string> in{"in_str"};
    std::string hello = "Hello";
    in(hello);
    EXPECT_EQ(std::get<0>(in.get()), "Hello");
    EXPECT_EQ(hello, "Hello"); // lvalue не перемещён
}

TEST(Input, CallbackFiresAndValueSurvives) {
    atp::io::input<int> in{"in_int"};
    int observed = 0;
    in.when([&](const int& v) { observed = v; });
    in(7);
    EXPECT_EQ(observed, 7);
    EXPECT_EQ(std::get<0>(in.get()), 7); // значение не «съедено» колбэком
}

TEST(Input, ResetClearsValue) {
    atp::io::input<int> in{"in_int"};
    in(42);
    in.reset();
    EXPECT_FALSE(in.has_value());
}

TEST(Input, MultiArgInput) {
    atp::io::input<int, std::string> in{"in_pair"};
    in(1, "one");
    EXPECT_EQ(std::get<0>(in.get()), 1);
    EXPECT_EQ(std::get<1>(in.get()), "one");
}

TEST(Input, ReentrantCallbackIsSafe) {
    atp::io::input<int> in{"in_int"};
    bool reentered = false;
    int outer_value_after_reentry = -1;
    in.when([&](const int& v) {
        if (!reentered) {
            reentered = true;
            in(100); // реентерабельный вызов перезаписывает value_
            // v привязан к snapshot-копии внешнего вызова — не повис
            outer_value_after_reentry = v;
        }
    });
    in(7);
    EXPECT_EQ(outer_value_after_reentry, 7);
    EXPECT_EQ(std::get<0>(in.get()), 100);
}

TEST(UnsafeInput, BehavesLikeInput) {
    atp::io::input<int> in{"in_int", atp::io::unsafe};
    int observed = 0;
    in.when([&](const int& v) { observed = v; });
    in(7);
    EXPECT_EQ(observed, 7);
    EXPECT_EQ(std::get<0>(in.get()), 7);
    in.reset();
    EXPECT_FALSE(in.has_value());
}

TEST(UnsafeQueuedInput, BehavesLikeQueuedInput) {
    atp::io::queued_input<int> in{"q_int", atp::io::unsafe};
    in(1);
    in(2);
    EXPECT_EQ(std::get<0>(in.pop()), 1);
    EXPECT_EQ(std::get<0>(in.pop()), 2);
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
    EXPECT_EQ(std::get<0>(in.pop()), 1);
    EXPECT_EQ(std::get<0>(in.pop()), 2);
    EXPECT_EQ(std::get<0>(in.pop()), 3);
    EXPECT_TRUE(in.empty());
}

TEST(QueuedInput, AcceptsLvalueWithoutMoving) {
    atp::io::queued_input<std::string> in{"q_str"};
    std::string hello = "Hello";
    in(hello);
    EXPECT_EQ(std::get<0>(in.pop()), "Hello");
    EXPECT_EQ(hello, "Hello"); // lvalue не перемещён
}

TEST(QueuedInput, MetadataMatchesSignature) {
    atp::io::queued_input<int> in{"q_int"};
    EXPECT_EQ(in.name(), "q_int");
    // Сигнатура та же, что у input<int>: вид входа не влияет на type()
    EXPECT_EQ(in.type(), std::type_index(typeid(std::tuple<int>)));
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
    EXPECT_EQ(std::get<0>(*item), 5);
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
    EXPECT_EQ(std::get<0>(items.front()), 1);
    EXPECT_EQ(std::get<0>(items.back()), 3);
}

TEST(QueuedInput, ConcurrentProducersLoseNothing) {
    atp::io::queued_input<int> in{"q_int"};
    constexpr int kThreads = 4;
    constexpr int kPerThread = 1000;
    {
        std::vector<std::jthread> producers;
        for (int t = 0; t < kThreads; ++t) {
            producers.emplace_back([&in] {
                for (int i = 0; i < kPerThread; ++i) {
                    in(i);
                }
            });
        }
    }
    EXPECT_EQ(in.size(), static_cast<std::size_t>(kThreads) * kPerThread);
}

TEST(QueuedInput, ProducerAndConsumerRunConcurrently) {
    atp::io::queued_input<int> in{"q_int"};
    constexpr int kCount = 5000;
    long long sum = 0;
    int received = 0;
    {
        std::jthread producer([&in] {
            for (int i = 1; i <= kCount; ++i) {
                in(i);
            }
        });
        while (received < kCount) {
            if (auto item = in.try_pop()) {
                sum += std::get<0>(*item);
                ++received;
            }
        }
    }
    EXPECT_EQ(sum, static_cast<long long>(kCount) * (kCount + 1) / 2);
}

TEST(QueuedInput, MultiArgPopsAsTuple) {
    atp::io::queued_input<int, std::string> in{"q_pair"};
    in(1, "one");
    in(2, "two");
    auto [num, text] = in.pop();
    EXPECT_EQ(num, 1);
    EXPECT_EQ(text, "one");
}

TEST(Input, ConcurrentWritersDeliverEveryCallbackWithOwnValue) {
    atp::io::input<int> in{"in_int"};
    std::atomic<int> calls{0};
    std::atomic<long long> sum{0};
    in.when([&](const int& v) {
        calls.fetch_add(1, std::memory_order_relaxed);
        sum.fetch_add(v, std::memory_order_relaxed);
    });
    constexpr int kThreads = 4;
    constexpr int kPerThread = 1000;
    {
        std::vector<std::jthread> writers;
        for (int t = 0; t < kThreads; ++t) {
            writers.emplace_back([&in] {
                for (int i = 1; i <= kPerThread; ++i) {
                    in(i);
                }
            });
        }
    }
    EXPECT_EQ(calls.load(), kThreads * kPerThread);
    // Сумма сходится, только если каждый колбэк получил снапшот своего вызова,
    // а не «последнее на момент вызова» значение.
    EXPECT_EQ(sum.load(),
              static_cast<long long>(kThreads) * kPerThread * (kPerThread + 1) / 2);
    EXPECT_TRUE(in.has_value());
}

TEST(InputsRegistry, TypedFieldAccess) {
    test_inputs ins;
    ins.input1(42);
    EXPECT_EQ(std::get<0>(ins.input1.get()), 42);
}

TEST(InputsRegistry, GetInputByNameReturnsMetadata) {
    test_inputs ins;
    const atp::io::input_base& in = ins.get_input("input1");
    EXPECT_EQ(in.name(), "input1");
    EXPECT_EQ(in.type(), std::type_index(typeid(std::tuple<int>)));
}

TEST(InputsRegistry, GetInputAliasesField) {
    test_inputs ins;
    ins.get<atp::io::input<int>>("input1")(100);
    EXPECT_EQ(std::get<0>(ins.input1.get()), 100); // то же поле, не копия
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
    EXPECT_EQ(std::get<0>(q.pop()), 1);
    EXPECT_EQ(std::get<0>(q.pop()), 2);
}

TEST(InputsRegistry, UnsafeInputsThroughRegistry) {
    atp::io::inputs ins;
    atp::io::input<int>& fast = ins.make<atp::io::input<int>>("fast", atp::io::unsafe);
    atp::io::queued_input<int>& q = ins.make<atp::io::queued_input<int>>("q", atp::io::unsafe);
    fast(1);
    q(2);
    EXPECT_EQ(std::get<0>(ins.get<atp::io::input<int>>("fast").get()), 1);
    EXPECT_EQ(std::get<0>(ins.get<atp::io::queued_input<int>>("q").pop()), 2);
}

TEST(InputsRegistry, SafetyIsNotPartOfTheType) {
    atp::io::inputs ins;
    ins.make<atp::io::input<int>>("safe");
    ins.make<atp::io::input<int>>("fast", atp::io::unsafe);
    // Политика блокировки — рантайм-свойство экземпляра: доступ по имени
    // одинаков для обоих входов и на политику не смотрит
    ins.get<atp::io::input<int>>("safe")(1);
    ins.get<atp::io::input<int>>("fast")(2);
    EXPECT_EQ(std::get<0>(ins.get<atp::io::input<int>>("safe").get()), 1);
    EXPECT_EQ(std::get<0>(ins.get<atp::io::input<int>>("fast").get()), 2);
}

TEST(InputsRegistry, KindMismatchThrows) {
    test_inputs ins;
    ins.make<atp::io::queued_input<int>>("q");
    // Сигнатуры совпадают (tuple<int>), но вид входа разный
    EXPECT_THROW((void)ins.get<atp::io::queued_input<int>>("input1"), std::runtime_error);
    EXPECT_THROW((void)ins.get<atp::io::input<int>>("q"), std::runtime_error);
}

TEST(InputsRegistry, UnknownNameThrows) {
    test_inputs ins;
    EXPECT_THROW((void)ins.get_input("no_such_input"), std::runtime_error);
    EXPECT_THROW((void)ins.get<atp::io::input<int>>("no_such_input"), std::runtime_error);
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
    EXPECT_THROW((duplicate_inputs{}), std::runtime_error);
}

TEST(InputsRegistry, DynamicInputCanBeRemoved) {
    test_inputs ins;
    atp::io::input<int>& extra = ins.make<atp::io::input<int>>("extra");
    extra(5);
    EXPECT_EQ(ins.list().size(), 3u);
    EXPECT_EQ(std::get<0>(ins.get<atp::io::input<int>>("extra").get()), 5);

    EXPECT_TRUE(ins.remove("extra"));
    EXPECT_EQ(ins.list().size(), 2u);
    EXPECT_THROW((void)ins.get_input("extra"), std::runtime_error);
    EXPECT_FALSE(ins.remove("extra"));
}
