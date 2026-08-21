// SPDX-License-Identifier: Apache-2.0
#include <concepts>
#include <cstddef>
#include <string>
#include <typeindex>
#include <typeinfo>

#include <gtest/gtest.h>

#include <atp/hosting.hpp>
#include <atp/module.hpp>

namespace {

struct decl_in : atp::io::inputs {
    atp::io::input<int>& value = make<int>("value");
    atp::io::queued_input<double>& events = make<atp::io::queued_input<double>>("events");
};
struct decl_out : atp::io::outputs {
    atp::io::output<std::string>& report = make<std::string>("report");
};
struct decl_props : atp::io::properties {
    atp::io::property<int>& limit = make("limit", 10);
    atp::io::property<std::string>& tmp = make<std::string>("tmp", "", atp::io::transient);
};
using decl_ports = atp::ports<decl_in, decl_out, decl_props>;

class declared_module : public atp::module<decl_ports, "declared"> {};

}  // namespace

static_assert(std::same_as<declared_module::ports_type, decl_ports>);
static_assert(atp::declares_ports<declared_module>);
static_assert(!atp::declares_ports<atp::module_base>);

TEST(ModuleDeclaration, PortsComeFromTheNodeInDeclarationOrder) {
    const atp::module_declaration decl = atp::declare_from_ports<decl_ports>();

    ASSERT_EQ(decl.inputs.size(), 2u);
    EXPECT_EQ(decl.inputs[0].name, "value");
    EXPECT_EQ(decl.inputs[0].type, std::type_index(typeid(int)));
    EXPECT_EQ(decl.inputs[1].name, "events");
    EXPECT_EQ(decl.inputs[1].type, std::type_index(typeid(double)));

    ASSERT_EQ(decl.outputs.size(), 1u);
    EXPECT_EQ(decl.outputs[0].name, "report");
    EXPECT_EQ(decl.outputs[0].type, std::type_index(typeid(std::string)));
}

TEST(ModuleDeclaration, PropertiesCarryDefaultKindAndPersistence) {
    const atp::module_declaration decl = atp::declare_from_ports<decl_ports>();

    ASSERT_EQ(decl.properties.size(), 2u);
    EXPECT_EQ(decl.properties[0].name, "limit");
    EXPECT_EQ(decl.properties[0].kind, atp::io::property_kind::number);
    EXPECT_EQ(decl.properties[0].default_value, "10");
    EXPECT_TRUE(decl.properties[0].options.empty());
    EXPECT_TRUE(decl.properties[0].persistent);
    EXPECT_FALSE(decl.properties[1].persistent);
}

TEST(ModuleDeclaration, TheSameAnswerFromAnObjectAndFromItsType) {
    const declared_module m;
    const atp::module_declaration from_object = atp::declare_from_module(m);
    const atp::module_declaration from_type = atp::declare_from_ports<decl_ports>();

    ASSERT_EQ(from_object.inputs.size(), from_type.inputs.size());
    for (std::size_t i = 0; i < from_type.inputs.size(); ++i) {
        EXPECT_EQ(from_object.inputs[i].name, from_type.inputs[i].name);
        EXPECT_EQ(from_object.inputs[i].type, from_type.inputs[i].type);
    }
    ASSERT_EQ(from_object.properties.size(), from_type.properties.size());
    EXPECT_EQ(from_object.properties[0].default_value, from_type.properties[0].default_value);
}
