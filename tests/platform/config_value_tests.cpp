// SPDX-License-Identifier: Apache-2.0
#include <cstdint>
#include <string>

#include <gtest/gtest.h>

#include <atp/config_value.hpp>

namespace {

using cv = atp::config_value;

cv sample() {
    return cv::object({
        {"channels", cv::array({1, 2})},
        {"rate", 48000.0},
        {"name", "rig"},
        {"muted", true},
    });
}

}  // namespace

TEST(ConfigValue, DefaultIsNull) {
    const cv value;
    EXPECT_TRUE(value.is_null());
    EXPECT_EQ(value.kind(), atp::config_kind::null);
    EXPECT_EQ(value.size(), 0U);
}

TEST(ConfigValue, IntegerAndRealAreDistinct) {
    const cv whole(3);
    const cv real(3.0);
    EXPECT_TRUE(whole.is_int());
    EXPECT_FALSE(whole.is_double());
    EXPECT_TRUE(real.is_double());
    EXPECT_FALSE(real.is_int());
}

TEST(ConfigValue, StringLiteralDoesNotBecomeBoolean) {
    const cv value("rig");
    EXPECT_TRUE(value.is_string());
    EXPECT_FALSE(value.is_bool());
    EXPECT_EQ(value.as_string(), "rig");
    EXPECT_TRUE(sample().at("name").is_string());
}

TEST(ConfigValue, ObjectKeepsDeclarationOrder) {
    const atp::config_value root = sample();
    ASSERT_EQ(root.size(), 4U);
    EXPECT_EQ(root.key_at(0), "channels");
    EXPECT_EQ(root.key_at(1), "rate");
    EXPECT_EQ(root.key_at(2), "name");
    EXPECT_EQ(root.key_at(3), "muted");
}

TEST(ConfigValue, FindAnswersNullptrForMissingKey) {
    const atp::config_value root = sample();
    EXPECT_NE(root.find("rate"), nullptr);
    EXPECT_EQ(root.find("absent"), nullptr);
}

TEST(ConfigValue, AtNamesTheMissingKey) {
    const atp::config_value root = sample();
    EXPECT_THROW((void)root.at("absent"), atp::bad_config);
    try {
        (void)root.at("absent");
        FAIL();
    } catch (const atp::bad_config& e) {
        EXPECT_NE(std::string(e.what()).find("absent"), std::string::npos);
    }
}

TEST(ConfigValue, TypedReadNamesTheKeyAndTheFoundKind) {
    const atp::config_value root = sample();
    EXPECT_EQ(root.int_at("channels", 0), 0);
    try {
        (void)root.int_at("name");
        FAIL();
    } catch (const atp::bad_config& e) {
        const std::string text = e.what();
        EXPECT_NE(text.find("name"), std::string::npos);
        EXPECT_NE(text.find("string"), std::string::npos);
    }
}

TEST(ConfigValue, TryReadsDoNotThrow) {
    const atp::config_value root = sample();
    EXPECT_EQ(root.at("rate").try_as_double(), 48000.0);
    EXPECT_FALSE(root.at("name").try_as_int().has_value());
    ASSERT_TRUE(root.at("name").try_as_string().has_value());
    EXPECT_EQ(*root.at("name").try_as_string(), "rig");
}

TEST(ConfigValue, ArrayIndexes) {
    const atp::config_value root = sample();
    const atp::config_value& channels = root.at("channels");
    ASSERT_TRUE(channels.is_array());
    ASSERT_EQ(channels.size(), 2U);
    EXPECT_EQ(channels[0].as_int(), 1);
    EXPECT_EQ(channels[1].as_int(), 2);
}

TEST(ConfigValue, ValueFallsBackWhenKeyIsAbsentOrWrongType) {
    const atp::config_value root = sample();
    EXPECT_EQ(root.value<std::int64_t>("absent", 7), 7);
    EXPECT_EQ(root.value<std::int64_t>("name", 7), 7);
    EXPECT_EQ(root.value<bool>("muted", false), true);
}
