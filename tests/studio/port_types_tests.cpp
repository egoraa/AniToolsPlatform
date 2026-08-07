// SPDX-License-Identifier: Apache-2.0
#include <any>
#include <map>
#include <optional>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <vector>

#include <gtest/gtest.h>

#include <atp/runtime/config_model.hpp>
#include <atp/studio/port_types.hpp>

namespace {

class fake_catalog {
   public:
    void add(const std::string& factory, atp::studio::module_info info) {
        info.name = factory;
        modules_.emplace(factory, std::move(info));
    }

    [[nodiscard]] atp::studio::describe_fn describer() const {
        return
            [this](const std::string& factory, const std::optional<atp::version>&) -> const atp::studio::module_info* {
                auto it = modules_.find(factory);
                return it == modules_.end() ? nullptr : &it->second;
            };
    }

   private:
    std::map<std::string, atp::studio::module_info> modules_;
};

atp::studio::port_info port(const std::string& name, std::type_index type) {
    return {name, type};
}

atp::studio::project make_project() {
    atp::studio::project proj = atp::studio::project::create();
    proj.add_module("", "source", "source");
    proj.add_module("", "sink", "sink");
    proj.add_module("", "text_sink", "text_sink");
    proj.add_module("", "any_sink", "any_sink");
    return proj;
}

fake_catalog make_catalog() {
    fake_catalog catalog;
    atp::studio::module_info source;
    source.outputs.push_back(port("value", typeid(int)));
    catalog.add("source", source);

    atp::studio::module_info sink;
    sink.inputs.push_back(port("value", typeid(int)));
    catalog.add("sink", sink);

    atp::studio::module_info text_sink;
    text_sink.inputs.push_back(port("value", typeid(std::string)));
    catalog.add("text_sink", text_sink);

    atp::studio::module_info any_sink;
    any_sink.inputs.push_back(port("value", typeid(std::any)));
    catalog.add("any_sink", any_sink);
    return catalog;
}

TEST(PortTypes, UniversalIsStdAnyAndNothingElse) {
    EXPECT_TRUE(atp::studio::is_universal(typeid(std::any)));
    EXPECT_FALSE(atp::studio::is_universal(typeid(int)));
    EXPECT_FALSE(atp::studio::is_universal(typeid(std::string)));
}

TEST(PortTypes, ConnectAcceptsMatchingTypesAndAnyInput) {
    atp::studio::project proj = make_project();
    const fake_catalog catalog = make_catalog();

    atp::studio::connect_ports(proj, "", "source.value", "sink.value", catalog.describer());
    atp::studio::connect_ports(proj, "", "source.value", "any_sink.value", catalog.describer());
    EXPECT_EQ(proj.group_at("")->connections.size(), 2u);
}

TEST(PortTypes, ConnectRejectsMismatchedTypesWithoutTouchingProject) {
    atp::studio::project proj = make_project();
    const fake_catalog catalog = make_catalog();

    EXPECT_THROW(atp::studio::connect_ports(proj, "", "source.value", "text_sink.value", catalog.describer()),
                 atp::runtime::config_error);
    EXPECT_TRUE(proj.group_at("")->connections.empty());
}

TEST(PortTypes, ConnectAllowsUnknownTypes) {
    atp::studio::project proj = make_project();
    fake_catalog catalog;

    atp::studio::connect_ports(proj, "", "source.value", "text_sink.value", catalog.describer());
    EXPECT_EQ(proj.group_at("")->connections.size(), 1u);
}

TEST(PortTypes, ResolvesSubgroupPortsThroughExpose) {
    atp::studio::project proj = atp::studio::project::create();
    proj.add_group("", "inner");
    proj.add_module("inner", "source", "source");
    proj.set_expose_output("inner", "out", "source.value");
    proj.add_module("", "sink", "sink");
    proj.add_module("", "text_sink", "text_sink");
    const fake_catalog catalog = make_catalog();

    const auto type = atp::studio::resolve_port_type(*proj.group_at(""), "inner.out", true, catalog.describer());
    ASSERT_TRUE(type.has_value());
    EXPECT_EQ(*type, std::type_index(typeid(int)));

    EXPECT_THROW(atp::studio::connect_ports(proj, "", "inner.out", "text_sink.value", catalog.describer()),
                 atp::runtime::config_error);
    atp::studio::connect_ports(proj, "", "inner.out", "sink.value", catalog.describer());
    EXPECT_EQ(proj.group_at("")->connections.size(), 1u);
}

TEST(PortTypes, ExposePortOutputUsesPortNameAlias) {
    atp::studio::project proj = make_project();
    const std::string alias = atp::studio::expose_port(proj, "", "source.value", true);
    EXPECT_EQ(alias, "value");
    const auto& outs = proj.group_at("")->expose_outputs;
    ASSERT_EQ(outs.size(), 1u);
    EXPECT_EQ(outs[0].first, "value");
    EXPECT_EQ(outs[0].second, "source.value");
    EXPECT_TRUE(proj.group_at("")->expose_inputs.empty());
}

TEST(PortTypes, ExposePortInputGoesToInputMap) {
    atp::studio::project proj = make_project();
    const std::string alias = atp::studio::expose_port(proj, "", "sink.value", false);
    EXPECT_EQ(alias, "value");
    const auto& ins = proj.group_at("")->expose_inputs;
    ASSERT_EQ(ins.size(), 1u);
    EXPECT_EQ(ins[0].second, "sink.value");
    EXPECT_TRUE(proj.group_at("")->expose_outputs.empty());
}

TEST(PortTypes, ExposePortDedupesAliasOnCollision) {
    atp::studio::project proj = make_project();
    proj.add_module("", "source", "source2");
    EXPECT_EQ(atp::studio::expose_port(proj, "", "source.value", true), "value");
    EXPECT_EQ(atp::studio::expose_port(proj, "", "source2.value", true), "value_2");
    EXPECT_EQ(proj.group_at("")->expose_outputs.size(), 2u);
}

TEST(PortTypes, ExposePortIsIdempotentForSamePort) {
    atp::studio::project proj = make_project();
    EXPECT_EQ(atp::studio::expose_port(proj, "", "source.value", true), "value");
    EXPECT_EQ(atp::studio::expose_port(proj, "", "source.value", true), "value");
    EXPECT_EQ(proj.group_at("")->expose_outputs.size(), 1u);
}

TEST(PortTypes, ExposeCandidatesListsModulePortsPerDirection) {
    atp::studio::project proj = make_project();
    const fake_catalog catalog = make_catalog();

    const auto outs = atp::studio::expose_candidates(*proj.group_at(""), false, catalog.describer());
    EXPECT_EQ(outs, (std::vector<std::string>{"source.value"}));

    const auto ins = atp::studio::expose_candidates(*proj.group_at(""), true, catalog.describer());
    EXPECT_EQ(ins, (std::vector<std::string>{"sink.value", "text_sink.value", "any_sink.value"}));
}

TEST(PortTypes, ExposeCandidatesTakesSubgroupPortsFromItsAliases) {
    atp::studio::project proj = atp::studio::project::create();
    proj.add_group("", "inner");
    proj.add_module("inner", "source", "source");
    proj.set_expose_output("inner", "out", "source.value");
    const fake_catalog catalog = make_catalog();

    EXPECT_EQ(atp::studio::expose_candidates(*proj.group_at(""), false, catalog.describer()),
              (std::vector<std::string>{"inner.out"}));
    EXPECT_TRUE(atp::studio::expose_candidates(*proj.group_at(""), true, catalog.describer()).empty());
}

TEST(PortTypes, ExposeCandidatesSkipsChildrenWithoutDescription) {
    atp::studio::project proj = make_project();
    const fake_catalog catalog;

    EXPECT_TRUE(atp::studio::expose_candidates(*proj.group_at(""), false, catalog.describer()).empty());
}

}  // namespace
