// SPDX-License-Identifier: Apache-2.0
#include <concepts>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include <atp/io.hpp>
#include <atp/module.hpp>

namespace {

struct node_in : atp::io::inputs {
    atp::io::input<int>& step = make<int>("step");
    atp::io::queued_input<int>& events = make<atp::io::queued_input<int>>("events");
};
struct node_out : atp::io::outputs {
    atp::io::output<int>& count = make<int>("count");
};
using node_ports = atp::ports<node_in, node_out>;

struct prop_section : atp::io::properties {
    atp::io::property<int>& limit = make("limit", 10);
    atp::io::property<std::string>& file = make<std::string>("file", "", atp::io::transient);
};

class node_module : public atp::module<node_ports, "node"> {
   public:
    using module::module;
};
class prop_module : public atp::module<atp::ports<atp::io::inputs, atp::io::outputs, prop_section>, "properties"> {};

}  // namespace

static_assert(std::same_as<node_ports::in_type, node_in>);
static_assert(std::same_as<node_ports::out_type, node_out>);
static_assert(std::same_as<node_ports::props_type, atp::io::properties>);
static_assert(atp::ports_list<node_ports>);
static_assert(atp::ports_list<atp::ports<>>);
static_assert(!atp::ports_list<int>);

TEST(Ports, SectionsAreIndependentRegistries) {
    node_module m;
    EXPECT_NE(m.inputs().find("step"), nullptr);
    EXPECT_NE(m.inputs().find("events"), nullptr);
    EXPECT_NE(m.outputs().find("count"), nullptr);
    EXPECT_EQ(m.inputs().find("count"), nullptr);
    EXPECT_EQ(m.outputs().find("step"), nullptr);
}

TEST(Ports, DefaultSectionsAreEmpty) {
    atp::module<> m;
    EXPECT_TRUE(m.inputs().list().empty());
    EXPECT_TRUE(m.outputs().list().empty());
    EXPECT_TRUE(m.properties().list().empty());
}

TEST(Ports, InputAndOutputMayShareName) {
    struct value_in : atp::io::inputs {
        atp::io::input<int>& value = make<int>("value");
    };
    struct value_out : atp::io::outputs {
        atp::io::output<int>& value = make<int>("value");
    };
    atp::module<atp::ports<value_in, value_out>> m;
    EXPECT_NE(m.inputs().find("value"), nullptr);
    EXPECT_NE(m.outputs().find("value"), nullptr);
}

TEST(Ports, MoveKeepsRefsAndConnections) {
    atp::io::input<int> target{"target"};
    node_ports p;
    p.out.count.connect(target);
    node_module m{std::move(p)};
    m.outputs().count(5);
    EXPECT_EQ(target.get(), 5);
    m.inputs().get<atp::io::input<int>>("step")(7);
    EXPECT_EQ(m.inputs().step.get(), 7);
}

TEST(Ports, ThirdSectionHoldsProperties) {
    prop_module m;
    EXPECT_EQ(m.properties().limit.get(), 10);
    EXPECT_FALSE(m.properties().file.persistent());
    EXPECT_EQ(m.properties().at("limit").to_string(), "10");
}

TEST(Ports, TwoSectionFormStaysValid) {
    atp::module<atp::ports<atp::io::inputs, atp::io::outputs>> m;
    EXPECT_TRUE(m.properties().list().empty());
}
