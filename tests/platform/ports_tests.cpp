// SPDX-License-Identifier: Apache-2.0
#include <concepts>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include <atp/io.hpp>

namespace {

struct node_in : atp::io::inputs {
    atp::io::input<int>& step = make<atp::io::input<int>>("step");
    atp::io::queued_input<int>& events = make<atp::io::queued_input<int>>("events");
};
struct node_out : atp::io::outputs {
    atp::io::output<int>& count = make<atp::io::output<int>>("count");
};
using node_ports = atp::io::ports<node_in, node_out>;

struct prop_section : atp::io::properties {
    atp::io::property<int>& limit = make<atp::io::property<int>>("limit", 10);
    atp::io::property<std::string>& file = make<atp::io::property<std::string>>("file", "", atp::io::transient);
};

}  // namespace

static_assert(std::same_as<node_ports::in_type, node_in>);
static_assert(std::same_as<node_ports::out_type, node_out>);
static_assert(std::same_as<node_ports::props_type, atp::io::properties>);
static_assert(atp::io::ports_node<node_ports>);
static_assert(atp::io::ports_node<atp::io::ports<>>);

TEST(Ports, SectionsAreIndependentRegistries) {
    node_ports p;
    EXPECT_NE(p.in.find("step"), nullptr);
    EXPECT_NE(p.in.find("events"), nullptr);
    EXPECT_NE(p.out.find("count"), nullptr);
    EXPECT_EQ(p.in.find("count"), nullptr);
    EXPECT_EQ(p.out.find("step"), nullptr);
}

TEST(Ports, DefaultSectionsAreEmpty) {
    atp::io::ports<> p;
    EXPECT_TRUE(p.in.list().empty());
    EXPECT_TRUE(p.out.list().empty());
}

TEST(Ports, InputAndOutputMayShareName) {
    struct value_in : atp::io::inputs {
        atp::io::input<int>& value = make<atp::io::input<int>>("value");
    };
    struct value_out : atp::io::outputs {
        atp::io::output<int>& value = make<atp::io::output<int>>("value");
    };
    atp::io::ports<value_in, value_out> p;
    EXPECT_NE(p.in.find("value"), nullptr);
    EXPECT_NE(p.out.find("value"), nullptr);
}

TEST(Ports, MoveKeepsRefsAndConnections) {
    atp::io::input<int> target{"target"};
    node_ports src;
    src.out.count.connect(target);
    node_ports dst{std::move(src)};
    dst.out.count(5);
    EXPECT_EQ(target.get(), 5);
    dst.in.get<atp::io::input<int>>("step")(7);
    EXPECT_EQ(dst.in.step.get(), 7);
}

TEST(Ports, ThirdSectionHoldsProperties) {
    atp::io::ports<atp::io::inputs, atp::io::outputs, prop_section> node;
    EXPECT_EQ(node.props.limit.get(), 10);
    EXPECT_FALSE(node.props.file.persistent());
    EXPECT_EQ(node.props.at("limit").to_string(), "10");
}

TEST(Ports, TwoSectionFormStaysValid) {
    atp::io::ports<atp::io::inputs, atp::io::outputs> node;
    EXPECT_TRUE(node.props.list().empty());
    static_assert(atp::io::ports_node<atp::io::ports<>>);
}
