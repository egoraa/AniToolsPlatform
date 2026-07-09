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

} // namespace

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
