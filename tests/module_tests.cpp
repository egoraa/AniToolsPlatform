#include <string>
#include <tuple>
#include <typeindex>
#include <typeinfo>

#include <gtest/gtest.h>

#include "platform/module.hpp"

namespace {

    struct test_inputs : atp::io::inputs {
        atp::io::input<int>& input1 = make<int>("input1");
        atp::io::input<std::string>& input2 = make<std::string>("input2");
    };

    class TestModule : public atp::Module<test_inputs, atp::io::outputs> {
    public:
        void initialize() override { initialized = true; }
        bool initialized = false;
    };

} // namespace

TEST(Module, InitializeOverrideRuns) {
    TestModule module;
    module.initialize();
    EXPECT_TRUE(module.initialized);
}

TEST(Module, InputsReturnsReference) {
    TestModule module;
    module.inputs().input1(42);
    std::string world = "World";
    module.inputs().input2(world);
    // записи не пропадают во временной копии — inputs() отдаёт ссылку на член
    EXPECT_EQ(std::get<0>(module.inputs().input1.get()), 42);
    EXPECT_EQ(std::get<0>(module.inputs().input2.get()), "World");
}

TEST(Module, NamedAccessThroughModule) {
    TestModule module;
    EXPECT_EQ(module.inputs().get_input("input1").type(),
              std::type_index(typeid(std::tuple<int>)));
    module.inputs().get_input<int>("input1")(7);
    EXPECT_EQ(std::get<0>(module.inputs().input1.get()), 7);
}

TEST(Module, ConstAccess) {
    TestModule module;
    module.inputs().input1(1);
    const TestModule& cmodule = module;
    EXPECT_TRUE(cmodule.inputs().input1.has_value());
}
