#include <any>
#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <thread>
#include <typeindex>
#include <typeinfo>
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
    EXPECT_EQ(hello, "Hello");  // lvalue не перемещён
}

TEST(Input, CallbackFiresAndValueSurvives) {
    atp::io::input<int> in{"in_int"};
    int observed = 0;
    in.when([&](const int& v) { observed = v; });
    in(7);
    EXPECT_EQ(observed, 7);
    EXPECT_EQ(in.get(), 7);  // значение не «съедено» колбэком
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
    EXPECT_EQ(in.take(), std::nullopt);  // второй take — уже пусто
}

TEST(Input, TakeOnEmptyReturnsNullopt) {
    atp::io::input<int> in{"in_int"};
    EXPECT_EQ(in.take(), std::nullopt);
}

TEST(Input, ReentrantCallbackIsSafe) {
    atp::io::input<int> in{"in_int"};
    bool reentered = false;
    int outer_value_after_reentry = -1;
    in.when([&](const int& v) {
        if (!reentered) {
            reentered = true;
            in(100);  // реентерабельный вызов перезаписывает value_
            // v привязан к snapshot-копии внешнего вызова — не повис
            outer_value_after_reentry = v;
        }
    });
    in(7);
    EXPECT_EQ(outer_value_after_reentry, 7);
    EXPECT_EQ(in.get(), 100);
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
    // Внутри должен лежать исходный int, а не std::any(std::any(42))
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

TEST(UnsafeInput, BehavesLikeInput) {
    atp::io::input<int> in{"in_int", atp::io::unsafe};
    int observed = 0;
    in.when([&](const int& v) { observed = v; });
    in(7);
    EXPECT_EQ(observed, 7);
    EXPECT_EQ(in.get(), 7);
    in.reset();
    EXPECT_TRUE(in.empty());
}

TEST(UnsafeQueuedInput, BehavesLikeQueuedInput) {
    atp::io::queued_input<int> in{"q_int", atp::io::unsafe};
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
    EXPECT_EQ(hello, "Hello");  // lvalue не перемещён
}

TEST(QueuedInput, MetadataMatchesSignature) {
    atp::io::queued_input<int> in{"q_int"};
    EXPECT_EQ(in.name(), "q_int");
    // Сигнатура та же, что у input<int>: вид входа не влияет на type()
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
    // Виртуальность: через ссылку на базу изымается голова очереди,
    // а не пустой value_ базы (контраст с невиртуальным get()).
    EXPECT_EQ(as_base.take(), std::optional<int>(1));
    EXPECT_EQ(q.size(), 1u);
}

TEST(QueuedInput, CallbackFiresOnPushAndValueStaysQueued) {
    atp::io::queued_input<int> in{"q_int"};
    int observed = 0;
    int calls = 0;
    in.when([&](const int& v) {
        observed = v;
        ++calls;
    });
    in(7);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(observed, 7);
    // Колбэк не «съел» значение — оно по-прежнему в очереди
    EXPECT_EQ(in.size(), 1u);
    EXPECT_EQ(in.pop(), 7);
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
                sum += *item;
                ++received;
            }
        }
    }
    EXPECT_EQ(sum, static_cast<long long>(kCount) * (kCount + 1) / 2);
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
    EXPECT_EQ(sum.load(), static_cast<long long>(kThreads) * kPerThread * (kPerThread + 1) / 2);
    EXPECT_FALSE(in.empty());
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
    EXPECT_EQ(ins.input1.get(), 100);  // то же поле, не копия
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
    atp::io::queued_input<int>& q = ins.make<atp::io::queued_input<int>>("q", atp::io::unsafe);
    fast(1);
    q(2);
    EXPECT_EQ(ins.get<atp::io::input<int>>("fast").get(), 1);
    EXPECT_EQ(ins.get<atp::io::queued_input<int>>("q").pop(), 2);
}

TEST(InputsRegistry, SafetyIsNotPartOfTheType) {
    atp::io::inputs ins;
    ins.make<atp::io::input<int>>("safe");
    ins.make<atp::io::input<int>>("fast", atp::io::unsafe);
    // Политика блокировки — рантайм-свойство экземпляра: доступ по имени
    // одинаков для обоих входов и на политику не смотрит
    ins.get<atp::io::input<int>>("safe")(1);
    ins.get<atp::io::input<int>>("fast")(2);
    EXPECT_EQ(ins.get<atp::io::input<int>>("safe").get(), 1);
    EXPECT_EQ(ins.get<atp::io::input<int>>("fast").get(), 2);
}

TEST(InputsRegistry, KindMismatchThrows) {
    test_inputs ins;
    ins.make<atp::io::queued_input<int>>("q");
    // Сигнатуры совпадают (typeid(int)), но вид входа разный
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
    EXPECT_THROW((duplicate_inputs{}), std::runtime_error);
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

TEST(Output, MetadataCarriesNameAndType) {
    atp::io::output<int> out{"out_int"};
    EXPECT_EQ(out.name(), "out_int");
    EXPECT_EQ(out.type(), std::type_index(typeid(int)));
}

TEST(Output, EmptyStateThrowsOnGet) {
    atp::io::output<int> out{"out_int"};
    EXPECT_TRUE(out.empty());
    EXPECT_THROW((void)out.get(), std::runtime_error);
}

TEST(Output, WriteWithoutTargetsOnlyCaches) {
    atp::io::output<int> out{"out_int"};
    out(42);
    ASSERT_FALSE(out.empty());
    EXPECT_EQ(out.get(), 42);
    EXPECT_EQ(out.connections(), 0u);
}

TEST(Output, AcceptsLvalueWithoutMoving) {
    atp::io::output<std::string> out{"out_str"};
    std::string hello = "Hello";
    out(hello);
    EXPECT_EQ(out.get(), "Hello");
    EXPECT_EQ(hello, "Hello");  // lvalue не перемещён
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
    EXPECT_EQ(out.get(), 7);  // кэш обновлён вместе с рассылкой
}

TEST(Output, DeliversToQueuedInput) {
    atp::io::output<int> out{"out_int"};
    atp::io::queued_input<int> q{"q"};
    out.connect(q);
    out(1);
    out(2);
    EXPECT_EQ(q.pop(), 1);
    EXPECT_EQ(q.pop(), 2);
    EXPECT_EQ(out.get(), 2);
}

TEST(Output, ResetClearsCacheButKeepsConnections) {
    atp::io::output<int> out{"out_int"};
    atp::io::input<int> in{"in"};
    out.connect(in);
    out(1);
    out.reset();
    EXPECT_TRUE(out.empty());
    EXPECT_EQ(out.connections(), 1u);
    out(2);  // соединение пережило reset — доставка работает
    EXPECT_EQ(in.get(), 2);
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
    EXPECT_FALSE(out.disconnect(in));  // повторный разрыв — уже не был подключён
    out(2);
    EXPECT_EQ(in.get(), 1);   // после разрыва доставки нет
    EXPECT_EQ(out.get(), 2);  // кэш при этом обновляется
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

TEST(Output, ReplayDeliversCacheOnConnect) {
    atp::io::output<int> out{"out_int"};
    atp::io::input<int> in{"in"};
    out(42);
    out.connect(in, atp::io::replay);
    EXPECT_EQ(in.get(), 42);
}

TEST(Output, ConnectWithoutReplayDoesNotDeliverCache) {
    atp::io::output<int> out{"out_int"};
    atp::io::input<int> in{"in"};
    out(42);
    out.connect(in);
    EXPECT_TRUE(in.empty());
}

TEST(Output, ReplayWithEmptyCacheDeliversNothing) {
    atp::io::output<int> out{"out_int"};
    atp::io::input<int> in{"in"};
    out.connect(in, atp::io::replay);
    EXPECT_TRUE(in.empty());
}

TEST(Output, TypeErasedConnectChecksCompatibility) {
    test_outputs outs;
    test_inputs ins;
    atp::io::output_base& out = outs.at("out1");
    out.connect(ins.at("input1"));  // int → int: совместимо
    outs.out1(5);
    EXPECT_EQ(ins.input1.get(), 5);
    // int → string: несовместимо, рантайм-проверка отклоняет
    EXPECT_THROW(out.connect(ins.at("input2")), std::runtime_error);
}

TEST(Output, TypeErasedConnectAcceptsQueuedInput) {
    atp::io::inputs ins;
    atp::io::queued_input<int>& q = ins.make<atp::io::queued_input<int>>("q");
    atp::io::output<int> out{"out_int"};
    // Выходу подходит любой наследник input<int> — в отличие от реестра,
    // где get<> требует точный вид входа
    static_cast<atp::io::output_base&>(out).connect(ins.at("q"), atp::io::replay);
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
    out.connect(any_in);  // типизированная перегрузка, без type-erased пути
    out(std::string("hello"));
    EXPECT_EQ(std::any_cast<std::string>(any_in.get()), "hello");
}

TEST(Output, ReplayBoxesCacheForAnyInput) {
    atp::io::output<int> out{"out_int"};
    atp::io::input<std::any> any_in{"any_in"};
    out(42);
    out.connect(any_in, atp::io::replay);
    EXPECT_EQ(std::any_cast<int>(any_in.get()), 42);
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
    out.connect(in);  // при T == std::any работает обычная типизированная пара
    out(std::any(42));
    EXPECT_EQ(in.get().type(), typeid(int));
    EXPECT_EQ(std::any_cast<int>(in.get()), 42);
}

TEST(Output, AnyOutputTypedConnectHasNoAmbiguity) {
    // При T == std::any any-перегрузка исключена requires-клаузой —
    // вызов с replay разрешается в обычную connect(input<T>&, replay_t)
    atp::io::output<std::any> out{"out_any"};
    atp::io::input<std::any> in{"in_any"};
    out.connect(in, atp::io::replay);
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
    // Дубликат ловится по адресу входа независимо от пути подключения
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
    EXPECT_EQ(std::any_cast<int>(any_in.get()), 1);  // после разрыва доставки нет
    EXPECT_EQ(out.connections(), 0u);
}

TEST(Output, ReentrantWriteBackIsSafe) {
    atp::io::output<int> out{"out_int"};
    atp::io::input<int> in{"in"};
    out.connect(in);
    bool reentered = false;
    in.when([&](const int& v) {
        if (!reentered) {
            reentered = true;
            out(v + 93);  // колбэк входа пишет обратно в тот же выход — нет дедлока
        }
    });
    out(7);
    EXPECT_EQ(in.get(), 100);
    EXPECT_EQ(out.get(), 100);
}

TEST(Output, ConcurrentWritersLoseNothingInQueuedTarget) {
    atp::io::output<int> out{"out_int"};
    atp::io::queued_input<int> q{"q"};
    out.connect(q);
    constexpr int kThreads = 4;
    constexpr int kPerThread = 1000;
    {
        std::vector<std::jthread> writers;
        for (int t = 0; t < kThreads; ++t) {
            writers.emplace_back([&out] {
                for (int i = 0; i < kPerThread; ++i) {
                    out(i);
                }
            });
        }
    }
    EXPECT_EQ(q.size(), static_cast<std::size_t>(kThreads) * kPerThread);
    EXPECT_FALSE(out.empty());
}

TEST(UnsafeOutput, BehavesLikeOutput) {
    atp::io::output<int> out{"out_int", atp::io::unsafe};
    atp::io::input<int> in{"in", atp::io::unsafe};
    out.connect(in);
    out(7);
    EXPECT_EQ(in.get(), 7);
    EXPECT_EQ(out.get(), 7);
    out.reset();
    EXPECT_TRUE(out.empty());
}

TEST(OutputsRegistry, TypedFieldAccess) {
    test_outputs outs;
    outs.out1(42);
    EXPECT_EQ(outs.out1.get(), 42);
    EXPECT_EQ(outs.get<atp::io::output<int>>("out1").get(), 42);  // то же поле, не копия
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
    EXPECT_THROW((duplicate_outputs{}), std::runtime_error);
}

TEST(OutputsRegistry, DynamicOutputCanBeRemoved) {
    test_outputs outs;
    atp::io::output<int>& extra = outs.make<atp::io::output<int>>("extra");
    extra(5);
    EXPECT_EQ(outs.list().size(), 3u);
    EXPECT_EQ(outs.get<atp::io::output<int>>("extra").get(), 5);

    EXPECT_TRUE(outs.remove("extra"));
    EXPECT_EQ(outs.list().size(), 2u);
    EXPECT_THROW((void)outs.at("extra"), std::runtime_error);
    EXPECT_FALSE(outs.remove("extra"));
}
