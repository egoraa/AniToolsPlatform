// SPDX-License-Identifier: Apache-2.0
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <atp/config/access_error.hpp>
#include <atp/module/dynamic_config.hpp>

namespace {

TEST(DynamicConfig, DeclaresTheFourScalarFormsAtTheirDefaults) {
    atp::dynamic_config cfg;
    cfg.scalar("muted", atp::field_kind::boolean, "true");
    cfg.scalar("rate", atp::field_kind::integer, "48000");
    cfg.scalar("gain", atp::field_kind::real, "1.5");
    cfg.scalar("preset", atp::field_kind::string, "flat");

    ASSERT_EQ(cfg.entries().size(), 4U);
    EXPECT_EQ(cfg.find("muted")->value<bool>(), true);
    EXPECT_EQ(cfg.find("rate")->value<std::int64_t>(), 48000);
    EXPECT_DOUBLE_EQ(cfg.find("gain")->value<double>(), 1.5);
    EXPECT_EQ(cfg.find("preset")->value<std::string>(), "flat");
}

TEST(DynamicConfig, EntriesComeInDeclarationOrder) {
    atp::dynamic_config cfg;
    cfg.scalar("second", atp::field_kind::integer, "2");
    cfg.scalar("first", atp::field_kind::integer, "1");

    ASSERT_EQ(cfg.entries().size(), 2U);
    EXPECT_EQ(cfg.entries()[0].name(), "second");
    EXPECT_EQ(cfg.entries()[1].name(), "first");
}

TEST(DynamicConfig, ARequiredScalarHasNoDefaultAndIsMarkedRequired) {
    atp::dynamic_config cfg;
    cfg.required_scalar("rate", atp::field_kind::integer);

    const atp::module_config::entry* e = cfg.find("rate");
    ASSERT_NE(e, nullptr);
    EXPECT_TRUE(e->required());
    EXPECT_FALSE(e->is_set());
}

TEST(DynamicConfig, ANonEmptyOptionSetMakesAnEnumeration) {
    atp::dynamic_config cfg;
    cfg.scalar("engine", atp::field_kind::string, "fm", {"fm", "additive"});

    const atp::module_config::entry* e = cfg.find("engine");
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->options(), (std::vector<std::string>{"fm", "additive"}));
    EXPECT_EQ(e->kind(), atp::field_kind::string);
}

TEST(DynamicConfig, ADefaultOutsideTheOptionSetThrows) {
    atp::dynamic_config cfg;
    EXPECT_THROW(cfg.scalar("engine", atp::field_kind::string, "wavetable", {"fm", "additive"}),
                 atp::config::access_error);
}

TEST(DynamicConfig, ADefaultThatDoesNotParseThrows) {
    atp::dynamic_config cfg;
    EXPECT_THROW(cfg.scalar("rate", atp::field_kind::integer, "quite fast"), atp::config::access_error);
}

TEST(DynamicConfig, AnObjectIsDeclaredIntoAndReadBack) {
    atp::dynamic_config cfg;
    cfg.object("master").scalar("gain", atp::field_kind::real, "1.0");

    const atp::module_config::entry* e = cfg.find("master");
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->kind(), atp::field_kind::object);
    EXPECT_DOUBLE_EQ(e->group().find("gain")->value<double>(), 1.0);
}

TEST(DynamicConfig, AnArrayOfScalarsCarriesTheOptionsListedAtTheDeclaration) {
    atp::dynamic_config cfg;
    cfg.scalar_list("routes", atp::field_kind::string, {"left", "right"});

    atp::module_config::entry* e = cfg.find("routes");
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->kind(), atp::field_kind::array);
    EXPECT_EQ(e->element(), atp::field_kind::string);
    e->resize(1);
    EXPECT_TRUE(e->set_element_from_string(0, "left"));
    EXPECT_FALSE(e->set_element_from_string(0, "centre"));
}

TEST(DynamicConfig, AnArrayOfObjectsGrowsThroughTheFactory) {
    atp::dynamic_config cfg;
    cfg.object_list("voices", [](atp::dynamic_config& v) { v.scalar("note", atp::field_kind::integer, "60"); });

    atp::module_config::entry* e = cfg.find("voices");
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->size(), 0U);
    e->resize(2);
    ASSERT_EQ(e->size(), 2U);
    EXPECT_EQ(e->group_at(0).find("note")->value<std::int64_t>(), 60);
    e->group_at(1).find("note")->set<std::int64_t>(72);
    EXPECT_EQ(e->group_at(1).find("note")->value<std::int64_t>(), 72);
    EXPECT_EQ(e->group_at(0).find("note")->value<std::int64_t>(), 60);
    e->resize(1);
    EXPECT_EQ(e->size(), 1U);
}

TEST(DynamicConfig, AnEmptyArrayOfObjectsStillDescribesItsElement) {
    atp::dynamic_config cfg;
    cfg.object_list("voices", [](atp::dynamic_config& v) { v.scalar("note", atp::field_kind::integer, "60"); });

    const atp::module_config::entry* e = cfg.find("voices");
    ASSERT_NE(e, nullptr);
    ASSERT_EQ(e->size(), 0U);
    EXPECT_EQ(e->element_shape().entries().size(), 1U);
    EXPECT_EQ(e->element_shape().entries()[0].name(), "note");
}

TEST(DynamicConfig, AnArrayOfArraysIsRefused) {
    atp::dynamic_config cfg;
    EXPECT_THROW(cfg.scalar_list("grid", atp::field_kind::array), atp::config::access_error);
}

TEST(DynamicConfig, ScalarRefusesAnObjectForm) {
    atp::dynamic_config cfg;
    EXPECT_THROW(cfg.scalar("master", atp::field_kind::object, ""), atp::config::access_error);
}

}  // namespace
