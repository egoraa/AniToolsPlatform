// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <atp/runtime/config_value_json.hpp>

TEST(ConfigValueJson, NullBecomesNull) {
    EXPECT_TRUE(atp::runtime::to_config_value(nlohmann::json()).is_null());
}

TEST(ConfigValueJson, WholeNumberBecomesIntegerAndFractionBecomesReal) {
    EXPECT_TRUE(atp::runtime::to_config_value(nlohmann::json::parse("3")).is_int());
    EXPECT_TRUE(atp::runtime::to_config_value(nlohmann::json::parse("3.0")).is_double());
    EXPECT_TRUE(atp::runtime::to_config_value(nlohmann::json::parse("3.5")).is_double());
}

TEST(ConfigValueJson, ObjectKeepsDocumentOrder) {
    const nlohmann::json doc = nlohmann::json::parse(R"({"zebra": 1, "alpha": 2})");
    const atp::config::node value = atp::runtime::to_config_value(doc);
    ASSERT_EQ(value.size(), 2U);
    EXPECT_EQ(value.key_at(0), "alpha");
    EXPECT_EQ(value.key_at(1), "zebra");
}

TEST(ConfigValueJson, NestedArraysAndObjects) {
    const nlohmann::json doc = nlohmann::json::parse(R"({"rig": {"channels": [1, 2, 6]}})");
    const atp::config::node value = atp::runtime::to_config_value(doc);
    const atp::config::node& channels = value.at("rig").at("channels");
    ASSERT_TRUE(channels.is_array());
    ASSERT_EQ(channels.size(), 3U);
    EXPECT_EQ(channels[2].as_int(), 6);
}

TEST(ConfigValueJson, BooleanAndStringSurvive) {
    const nlohmann::json doc = nlohmann::json::parse(R"({"muted": true, "name": "rig"})");
    const atp::config::node value = atp::runtime::to_config_value(doc);
    EXPECT_TRUE(value.bool_at("muted"));
    EXPECT_EQ(value.string_at("name"), "rig");
}

TEST(ConfigValueJson, UnsignedBeyondSignedRangeIsRefused) {
    const nlohmann::json doc = nlohmann::json::parse(R"({"huge": 18446744073709551615})");
    EXPECT_THROW((void)atp::runtime::to_config_value(doc), atp::config::access_error);
}
