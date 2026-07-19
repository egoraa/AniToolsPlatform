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

    in.set_notifier(nullptr);  // снятый уведомитель молчит
    out(8);
    EXPECT_EQ(n.count, 1);
}

TEST(InputNotifier, ReplayDeliveryNotifiesLateSubscriber) {
    atp::io::output<int> out("out");
    out(5);

    atp::io::input<int> in("in");
    counting_notifier n;
    in.set_notifier(&n);
    out.connect(in, atp::io::replay);  // доставка кэша — тоже доставка

    EXPECT_EQ(n.count, 1);
    EXPECT_EQ(in.get(), 5);
}

TEST(InputNotifier, DirectWriteDoesNotNotify) {
    atp::io::input<int> in("in");
    counting_notifier n;
    in.set_notifier(&n);
    in(3);  // прямая запись — не доставка от выхода, сигналить нечего
    EXPECT_EQ(n.count, 0);
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

TEST(Watcher, FiresHandlerOnFreshValue) {
    atp::io::input<int> in{"in_int"};
    atp::io::watcher w;
    int observed = 0;
    w.watch(in, [&](const int& v) { observed = v; });
    EXPECT_EQ(w.poll(), atp::io::work_status::idle);  // писем не было
    in(7);
    EXPECT_EQ(w.poll(), atp::io::work_status::busy);
    EXPECT_EQ(observed, 7);
    EXPECT_TRUE(in.empty());                          // значение изъято правилом
    EXPECT_EQ(w.poll(), atp::io::work_status::idle);  // повторно не срабатывает
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
        std::jthread writer([&in] { in(7); });  // запись с чужого потока
    }
    EXPECT_EQ(w.poll(), atp::io::work_status::busy);
    EXPECT_EQ(handler_thread, std::this_thread::get_id());  // обработчик — на потоке poll
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

TEST(IoBase, ThreadSafeReflectsConstructionTag) {
    atp::io::input<int> guarded("guarded");  // safe — умолчание
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
    EXPECT_EQ(regs.find("mirror"), &real);  // тот же объект, не копия
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
    EXPECT_EQ(regs.list().size(), 2u);  // list видит оба вида записей
}

TEST(IoRegistry, DestructionLeavesAliasedPortAlive) {
    atp::io::input<int> foreign{"foreign"};
    {
        atp::io::inputs regs;
        regs.alias("mirror", foreign);
    }  // реестр умер — алиас не владел
    foreign(5);
    EXPECT_EQ(foreign.get(), 5);
}
