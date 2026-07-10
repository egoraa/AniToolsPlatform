#include <string>
#include <typeindex>
#include <typeinfo>

#include <gtest/gtest.h>

#include "platform/module.hpp"

namespace {

    struct test_inputs : atp::io::inputs {
        atp::io::input<int>& input1 = make<atp::io::input<int>>("input1");
        atp::io::input<std::string>& input2 = make<atp::io::input<std::string>>("input2");
    };

    class test_module : public atp::module<test_inputs, atp::io::outputs> {
    public:
        void initialize() override { initialized = true; }
        bool initialized = false;
    };

    class versioned_module
        : public atp::module<test_inputs, atp::io::outputs, atp::version{1, 2, 3}> {};

} // namespace

// Compile-time доступ к версии — прямо из типа модуля.
static_assert(versioned_module::module_version == atp::version{1, 2, 3});
static_assert(test_module::module_version == atp::default_version);

TEST(Module, InitializeOverrideRuns) {
    test_module module;
    module.initialize();
    EXPECT_TRUE(module.initialized);
}

TEST(Module, InputsReturnsReference) {
    test_module module;
    module.inputs().input1(42);
    std::string world = "World";
    module.inputs().input2(world);
    // записи не пропадают во временной копии — inputs() отдаёт ссылку на член
    EXPECT_EQ(module.inputs().input1.get(), 42);
    EXPECT_EQ(module.inputs().input2.get(), "World");
}

TEST(Module, NamedAccessThroughModule) {
    test_module module;
    EXPECT_EQ(module.inputs().at("input1").type(),
              std::type_index(typeid(int)));
    module.inputs().get<atp::io::input<int>>("input1")(7);
    EXPECT_EQ(module.inputs().input1.get(), 7);
}

TEST(Module, ConstAccess) {
    test_module module;
    module.inputs().input1(1);
    const test_module& cmodule = module;
    EXPECT_FALSE(cmodule.inputs().input1.empty());
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
