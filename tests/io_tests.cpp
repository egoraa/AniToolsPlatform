#include <stdexcept>
#include <string>
#include <tuple>
#include <typeindex>
#include <typeinfo>

#include <gtest/gtest.h>

#include "platform/io.hpp"

namespace {

    struct test_inputs : atp::io::inputs {
        atp::io::inputs::input<int> input1{*this, "input1"};
        atp::io::inputs::input<std::string> input2{*this, "input2"};
    };

} // namespace

TEST(BasicInput, MetadataCarriesNameAndType) {
    atp::io::basic_input<int> in{"in_int"};
    EXPECT_EQ(in.info().name, "in_int");
    EXPECT_EQ(in.info().type, std::type_index(typeid(std::tuple<int>)));
    EXPECT_EQ(in.info().type_hash, typeid(std::tuple<int>).hash_code());
}

TEST(BasicInput, EmptyStateThrowsOnGet) {
    atp::io::basic_input<int> in{"in_int"};
    EXPECT_FALSE(in.has_value());
    EXPECT_THROW((void)in.get(), std::runtime_error);
}

TEST(BasicInput, AcceptsRvalue) {
    atp::io::basic_input<int> in{"in_int"};
    in(42);
    ASSERT_TRUE(in.has_value());
    EXPECT_EQ(std::get<0>(in.get()), 42);
}

TEST(BasicInput, AcceptsLvalueWithoutMoving) {
    atp::io::basic_input<std::string> in{"in_str"};
    std::string hello = "Hello";
    in(hello);
    EXPECT_EQ(std::get<0>(in.get()), "Hello");
    EXPECT_EQ(hello, "Hello"); // lvalue не перемещён
}

TEST(BasicInput, CallbackFiresAndValueSurvives) {
    atp::io::basic_input<int> in{"in_int"};
    int observed = 0;
    in.when([&](const int& v) { observed = v; });
    in(7);
    EXPECT_EQ(observed, 7);
    EXPECT_EQ(std::get<0>(in.get()), 7); // значение не «съедено» колбэком
}

TEST(BasicInput, ResetClearsValue) {
    atp::io::basic_input<int> in{"in_int"};
    in(42);
    in.reset();
    EXPECT_FALSE(in.has_value());
}

TEST(BasicInput, MultiArgInput) {
    atp::io::basic_input<int, std::string> in{"in_pair"};
    in(1, "one");
    EXPECT_EQ(std::get<0>(in.get()), 1);
    EXPECT_EQ(std::get<1>(in.get()), "one");
}

TEST(BasicInput, ReentrantCallbackIsSafe) {
    atp::io::basic_input<int> in{"in_int"};
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

TEST(InputsRegistry, TypedFieldAccess) {
    test_inputs ins;
    ins.input1(42);
    EXPECT_EQ(std::get<0>(ins.input1.get()), 42);
}

TEST(InputsRegistry, GetInputInfoByName) {
    test_inputs ins;
    const atp::io::input_info& info = ins.get_input_info("input1");
    EXPECT_EQ(info.name, "input1");
    EXPECT_EQ(info.type, std::type_index(typeid(std::tuple<int>)));
}

TEST(InputsRegistry, GetInputAliasesField) {
    test_inputs ins;
    ins.get_input<int>("input1")(100);
    EXPECT_EQ(std::get<0>(ins.input1.get()), 100); // то же поле, не копия
}

TEST(InputsRegistry, WrongTypeThrows) {
    test_inputs ins;
    EXPECT_THROW((void)ins.get_input<std::string>("input1"), std::runtime_error);
}

TEST(InputsRegistry, UnknownNameThrows) {
    test_inputs ins;
    EXPECT_THROW((void)ins.get_input_info("no_such_input"), std::runtime_error);
    EXPECT_THROW((void)ins.get_input<int>("no_such_input"), std::runtime_error);
}

TEST(InputsRegistry, ListEnumeratesAll) {
    test_inputs ins;
    EXPECT_EQ(ins.list().size(), 2u);
}

TEST(InputsRegistry, DuplicateNameThrowsOnConstruction) {
    struct duplicate_inputs : atp::io::inputs {
        atp::io::inputs::input<int> a{*this, "same"};
        atp::io::inputs::input<int> b{*this, "same"};
    };
    EXPECT_THROW((duplicate_inputs{}), std::runtime_error);
}

TEST(InputsRegistry, DestroyedInputUnregisters) {
    test_inputs ins;
    {
        atp::io::inputs::input<int> extra{ins, "extra"};
        EXPECT_EQ(ins.list().size(), 3u);
        EXPECT_EQ(ins.get_input_info("extra").name, "extra");
    }
    EXPECT_EQ(ins.list().size(), 2u);
    EXPECT_THROW((void)ins.get_input_info("extra"), std::runtime_error);
}
