#include <stop_token>
#include <string>
#include <string_view>
#include <typeindex>
#include <typeinfo>
#include <utility>

#include <gtest/gtest.h>

#include <atp/module.hpp>

namespace {

struct test_in : atp::io::inputs {
    atp::io::input<int>& input1 = make<atp::io::input<int>>("input1");
    atp::io::input<std::string>& input2 = make<atp::io::input<std::string>>("input2");
};
using test_ports = atp::io::ports<test_in>;

class test_module : public atp::module<test_ports> {
   public:
    using module::module;  // конструктор с готовым узлом — для теста переноса
    void initialize(atp::module_context&) override {
        initialized = true;
    }
    bool initialized = false;
};

class versioned_module : public atp::module<test_ports, "", atp::version{1, 2, 3}> {};

// имя вторым NTTP-параметром; версия не указана — default_version
class named_module : public atp::module<test_ports, "named"> {};

// имя и версия вместе
class full_module : public atp::module<test_ports, "full", atp::version{1, 2, 3}> {};

}  // namespace

// Compile-time доступ к версии — прямо из типа модуля.
static_assert(versioned_module::module_version == atp::version{1, 2, 3});
static_assert(test_module::module_version == atp::default_version);

// Compile-time доступ к имени — прямо из типа модуля.
static_assert(test_module::module_name.empty());
static_assert(named_module::module_name == "named");
static_assert(full_module::module_name == "full");
static_assert(named_module::module_version == atp::default_version);
static_assert(full_module::module_version == atp::version{1, 2, 3});

TEST(Module, InitializeOverrideRuns) {
    atp::service_directory services;
    atp::module_context context{services};
    test_module module;
    module.initialize(context);
    EXPECT_TRUE(module.initialized);
}

TEST(Module, InputsReturnsReference) {
    test_module module;
    module.inputs().input1(42);
    std::string world = "World";
    module.inputs().input2(world);
    // записи не пропадают во временной копии — inputs() отдаёт ссылку на секцию
    EXPECT_EQ(module.inputs().input1.get(), 42);
    EXPECT_EQ(module.inputs().input2.get(), "World");
}

TEST(Module, NamedAccessThroughModule) {
    test_module module;
    EXPECT_EQ(module.inputs().at("input1").type(), std::type_index(typeid(int)));
    module.inputs().get<atp::io::input<int>>("input1")(7);
    EXPECT_EQ(module.inputs().input1.get(), 7);
}

TEST(Module, ConstAccess) {
    test_module module;
    module.inputs().input1(1);
    const test_module& cmodule = module;
    EXPECT_FALSE(cmodule.inputs().input1.empty());
}

TEST(Module, PortsAreNodePorts) {
    // Порты модуля — это порты переданного узла. Сравниваются адреса
    // именно портов, а не секций: секции — члены узла и переносятся по
    // значению, сами же порты живут в куче и перенос их не трогает.
    test_ports ports;
    atp::io::input<int>* input1 = &ports.in.input1;
    atp::io::input<std::string>* input2 = &ports.in.input2;
    test_module module{std::move(ports)};
    EXPECT_EQ(&module.inputs().input1, input1);
    EXPECT_EQ(&module.inputs().input2, input2);
}

TEST(Module, ConstructedFromPrewiredPorts) {
    // Узел коммутируется до модуля; модуль забирает его конструктором,
    // соединение переживает перенос (порты в куче, move их не трогает).
    atp::io::output<int> feeder{"feeder"};
    test_ports ports;
    feeder.connect(ports.in.input1);
    test_module module{std::move(ports)};
    feeder(11);
    EXPECT_EQ(module.inputs().input1.get(), 11);
}

TEST(Module, DefaultVersionThroughBase) {
    test_module module;
    atp::module_base& base = module;
    // Модуль, не объявивший версию, отвечает 0.0.1
    EXPECT_EQ(base.get_version(), atp::default_version);
    EXPECT_EQ(base.get_version(), atp::version(0, 0, 1));
}

TEST(Module, DeclaredVersionThroughBase) {
    versioned_module module;
    atp::module_base& base = module;
    EXPECT_EQ(base.get_version(), atp::version(1, 2, 3));
    // Сравнение с версией другой длины: 1.2 == 1.2.0 < 1.2.3
    EXPECT_GT(base.get_version(), atp::version(1, 2));
}

TEST(Module, DefaultNameThroughBaseIsEmpty) {
    test_module module;
    atp::module_base& base = module;
    // Модуль, не объявивший имени, — «аноним»: пустой string_view
    EXPECT_EQ(base.get_name(), std::string_view{});
}

TEST(Module, DeclaredNameThroughBase) {
    named_module module;
    atp::module_base& base = module;
    EXPECT_EQ(base.get_name(), "named");
}

namespace {
struct erased_probe_in : atp::io::inputs {
    atp::io::input<int>& number = make<atp::io::input<int>>("number");
};
using erased_probe_ports = atp::io::ports<erased_probe_in>;
class erased_probe : public atp::module<erased_probe_ports> {};
}  // namespace

TEST(Module, IoRegistriesReachableThroughBase) {
    erased_probe m;
    atp::module_base& base = m;
    // Реестры через type-erased базу — те же объекты, что у конкретного типа.
    EXPECT_EQ(&base.inputs(), static_cast<atp::io::inputs*>(&m.inputs()));
    EXPECT_NE(base.inputs().find("number"), nullptr);
    EXPECT_EQ(base.outputs().list().size(), 0u);
}

TEST(Module, DefaultIterateReportsIdle) {
    erased_probe m;
    // Умолчание module<>: no-op-итерация и есть простой.
    EXPECT_EQ(m.iterate(std::stop_token{}), atp::work_status::idle);
}

namespace {

struct counter_props : atp::io::properties {
    atp::io::property<int>& step = make<atp::io::property<int>>("step", 1);
};
using counter_props_ports = atp::io::ports<atp::io::inputs, atp::io::outputs, counter_props>;

class propertied_module : public atp::module<counter_props_ports, "propertied"> {};

}  // namespace

TEST(Module, PropertiesCovariantAccess) {
    propertied_module m;
    EXPECT_EQ(m.properties().step.get(), 1);  // конкретный тип видит секцию
}

TEST(Module, PropertiesReachableThroughBase) {
    propertied_module m;
    atp::module_base& base = m;
    atp::io::property_base* p = base.properties().find("step");
    ASSERT_NE(p, nullptr);
    p->from_string("5");
    EXPECT_EQ(m.properties().step.get(), 5);
    EXPECT_TRUE(m.properties().step.changed());
}

TEST(Module, DefaultModuleHasEmptyProperties) {
    atp::module<> m;
    EXPECT_TRUE(m.properties().list().empty());
}
