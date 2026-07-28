#include <concepts>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include <atp/io.hpp>

namespace {

// The sections are ordinary registry heirs; the node gathers them.
struct node_in : atp::io::inputs {
    atp::io::input<int>& step = make<atp::io::input<int>>("step");
    atp::io::queued_input<int>& events = make<atp::io::queued_input<int>>("events");
};
struct node_out : atp::io::outputs {
    atp::io::output<int>& count = make<atp::io::output<int>>("count");
};
using node_ports = atp::io::ports<node_in, node_out>;

// The third section holds the properties, declared with the same make<>, with the default and the
// tags forwarded to the property's constructor.
struct prop_section : atp::io::properties {
    atp::io::property<int>& limit = make<atp::io::property<int>>("limit", 10);
    atp::io::property<std::string>& file = make<atp::io::property<std::string>>("file", "", atp::io::transient);
};

}  // namespace

// The node reports its section types as members, and module<> declares the covariant
// inputs()/outputs() from them.
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
    // The sections are separate registries, so a port name may repeat across them.
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
    // The connection survived the move: the ports are on the heap and the move left them alone.
    dst.out.count(5);
    EXPECT_EQ(target.get(), 5);
    // The destination sections' reference members point at objects the destination itself owns.
    dst.in.get<atp::io::input<int>>("step")(7);
    EXPECT_EQ(dst.in.step.get(), 7);
}

TEST(Ports, ThirdSectionHoldsProperties) {
    atp::io::ports<atp::io::inputs, atp::io::outputs, prop_section> node;
    EXPECT_EQ(node.props.limit.get(), 10);
    EXPECT_FALSE(node.props.file.persistent());
    // type-erased access through the registry, by the same machinery as the ports
    EXPECT_EQ(node.props.at("limit").to_string(), "10");
}

TEST(Ports, TwoSectionFormStaysValid) {
    // backwards compatibility: the older form without a properties section
    atp::io::ports<atp::io::inputs, atp::io::outputs> node;
    EXPECT_TRUE(node.props.list().empty());
    static_assert(atp::io::ports_node<atp::io::ports<>>);
}
