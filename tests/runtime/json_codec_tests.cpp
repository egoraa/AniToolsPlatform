// SPDX-License-Identifier: Apache-2.0
#include <cstdint>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include <atp/config/node.hpp>
#include <atp/runtime/config_error.hpp>
#include <atp/runtime/json_codec.hpp>

TEST(JsonCodec, ParsesEveryForm) {
    const atp::config::node doc =
        atp::runtime::json_parse(R"({"n": null, "b": true, "i": 3, "r": 3.5, "s": "rig", "a": [1, 2], "o": {"k": 1}})");
    ASSERT_TRUE(doc.is_object());
    EXPECT_TRUE(doc.at("n").is_null());
    EXPECT_TRUE(doc.at("b").as_bool());
    EXPECT_TRUE(doc.at("i").is_int());
    EXPECT_TRUE(doc.at("r").is_double());
    EXPECT_EQ(doc.at("s").as_string(), "rig");
    EXPECT_EQ(doc.at("a").size(), 2U);
    EXPECT_EQ(doc.at("o").int_at("k"), 1);
}

TEST(JsonCodec, WholeNumberStaysAnIntegerAndAFractionAReal) {
    EXPECT_TRUE(atp::runtime::json_parse("3").is_int());
    EXPECT_TRUE(atp::runtime::json_parse("3.0").is_double());
}

TEST(JsonCodec, DumpSortsObjectKeys) {
    atp::config::node doc;
    doc["zebra"] = atp::config::node(1);
    doc["alpha"] = atp::config::node(2);
    EXPECT_EQ(atp::runtime::json_dump(doc), R"({"alpha":2,"zebra":1})");
}

TEST(JsonCodec, DumpIndentsWhenAsked) {
    atp::config::node doc;
    doc["a"] = atp::config::node(1);
    EXPECT_EQ(atp::runtime::json_dump(doc, 4), "{\n    \"a\": 1\n}");
}

TEST(JsonCodec, TextRoundTripsThroughTheTree) {
    const std::string text = R"({"alpha":[1,2.5,"x",true,null],"beta":{"in":{"k":"v"}}})";
    EXPECT_EQ(atp::runtime::json_dump(atp::runtime::json_parse(text)), text);
}

TEST(JsonCodec, ARealKeepsItsShortestSpellingThroughATrip) {
    const std::string text = R"({"gain":0.1})";
    EXPECT_EQ(atp::runtime::json_dump(atp::runtime::json_parse(text)), text);
}

TEST(JsonCodec, BrokenTextThrowsConfigError) {
    EXPECT_THROW((void)atp::runtime::json_parse("{"), atp::runtime::config_error);
    EXPECT_THROW((void)atp::runtime::json_parse(""), atp::runtime::config_error);
}

TEST(JsonCodec, TryParseAnswersNulloptInsteadOfThrowing) {
    EXPECT_FALSE(atp::runtime::try_json_parse("{").has_value());
    EXPECT_FALSE(atp::runtime::try_json_parse("").has_value());
    const std::optional<atp::config::node> parsed = atp::runtime::try_json_parse("7");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->as_int(), 7);
}

TEST(JsonCodec, ANumberBeyondInt64IsAConfigError) {
    EXPECT_THROW((void)atp::runtime::json_parse("9223372036854775808"), atp::runtime::config_error);
    EXPECT_FALSE(atp::runtime::try_json_parse("9223372036854775808").has_value());
}
