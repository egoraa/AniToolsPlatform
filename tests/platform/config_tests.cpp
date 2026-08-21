// SPDX-License-Identifier: Apache-2.0
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <atp/config.hpp>

namespace {

using cv = atp::config::node;

cv sample() {
    return cv::object({
        {"channels", cv::array({1, 2})},
        {"rate", 48000.0},
        {"name", "rig"},
        {"muted", true},
    });
}

}  // namespace

TEST(Config, DefaultIsNull) {
    const cv value;
    EXPECT_TRUE(value.is_null());
    EXPECT_EQ(value.kind(), atp::config::kind::null);
    EXPECT_EQ(value.size(), 0U);
}

TEST(Config, IntegerAndRealAreDistinct) {
    const cv whole(3);
    const cv real(3.0);
    EXPECT_TRUE(whole.is_int());
    EXPECT_FALSE(whole.is_double());
    EXPECT_TRUE(real.is_double());
    EXPECT_FALSE(real.is_int());
}

TEST(Config, StringLiteralDoesNotBecomeBoolean) {
    const cv value("rig");
    EXPECT_TRUE(value.is_string());
    EXPECT_FALSE(value.is_bool());
    EXPECT_EQ(value.as_string(), "rig");
    EXPECT_TRUE(sample().at("name").is_string());
}

TEST(Config, ObjectKeepsDeclarationOrder) {
    const atp::config::node root = sample();
    ASSERT_EQ(root.size(), 4U);
    EXPECT_EQ(root.key_at(0), "channels");
    EXPECT_EQ(root.key_at(1), "rate");
    EXPECT_EQ(root.key_at(2), "name");
    EXPECT_EQ(root.key_at(3), "muted");
}

TEST(Config, FindAnswersNullptrForMissingKey) {
    const atp::config::node root = sample();
    EXPECT_NE(root.find("rate"), nullptr);
    EXPECT_EQ(root.find("absent"), nullptr);
}

TEST(Config, AtNamesTheMissingKey) {
    const atp::config::node root = sample();
    EXPECT_THROW((void)root.at("absent"), atp::config::access_error);
    try {
        (void)root.at("absent");
        FAIL();
    } catch (const atp::config::access_error& e) {
        EXPECT_NE(std::string(e.what()).find("absent"), std::string::npos);
    }
}

TEST(Config, TypedReadNamesTheKeyAndTheFoundKind) {
    const atp::config::node root = sample();
    try {
        (void)root.int_at("name");
        FAIL();
    } catch (const atp::config::access_error& e) {
        const std::string text = e.what();
        EXPECT_NE(text.find("name"), std::string::npos);
        EXPECT_NE(text.find("string"), std::string::npos);
    }
}

TEST(Config, TryReadsDoNotThrow) {
    const atp::config::node root = sample();
    EXPECT_EQ(root.at("rate").try_as_double(), 48000.0);
    EXPECT_FALSE(root.at("name").try_as_int().has_value());
    ASSERT_TRUE(root.at("name").try_as_string().has_value());
    EXPECT_EQ(*root.at("name").try_as_string(), "rig");
}

TEST(Config, ArrayIndexes) {
    const atp::config::node root = sample();
    const atp::config::node& channels = root.at("channels");
    ASSERT_TRUE(channels.is_array());
    ASSERT_EQ(channels.size(), 2U);
    EXPECT_EQ(channels[0].as_int(), 1);
    EXPECT_EQ(channels[1].as_int(), 2);
}

TEST(Config, ObjectKeepsInsertionOrder) {
    const atp::config::node root = atp::config::node::object({{"b", 1}, {"a", 2}});
    ASSERT_EQ(root.kind(), atp::config::kind::object);
    ASSERT_EQ(root.size(), 2U);
    EXPECT_EQ(root.key_at(0), "b");
    EXPECT_EQ(root.key_at(1), "a");
}

TEST(Config, ReadWithFallbackTakesANullableNode) {
    const atp::config::node root = atp::config::node::object({{"rate", 48000}});
    EXPECT_EQ(atp::config::int_or(root.find("rate"), 0), 48000);
    EXPECT_EQ(atp::config::int_or(root.find("missing"), 44100), 44100);
    EXPECT_THROW((void)root.at("missing"), atp::config::access_error);
}

TEST(Config, EqualityDistinguishesEveryForm) {
    EXPECT_EQ(cv(), cv());
    EXPECT_NE(cv(), cv(cv::object_type{}));
    EXPECT_NE(cv(), cv(cv::array_type{}));
    EXPECT_EQ(cv(3), cv(3));
    EXPECT_NE(cv(3), cv(3.0));
    EXPECT_NE(cv(true), cv(1));
    EXPECT_EQ(cv("rig"), cv("rig"));
    EXPECT_NE(cv("rig"), cv("rug"));
    EXPECT_EQ(sample(), sample());
}

TEST(Config, EqualityIsRecursiveAndOrderSensitiveForObjects) {
    const cv straight = cv::object({{"a", 1}, {"b", 2}});
    const cv swapped = cv::object({{"b", 2}, {"a", 1}});
    EXPECT_EQ(straight, cv::object({{"a", 1}, {"b", 2}}));
    EXPECT_NE(straight, swapped);
    EXPECT_NE(cv::array({1, 2}), cv::array({2, 1}));
    EXPECT_EQ(cv::object({{"in", cv::array({1, 2})}}), cv::object({{"in", cv::array({1, 2})}}));
}

TEST(Config, SubscriptBuildsAnObjectOutOfNull) {
    cv doc;
    doc["version"] = cv("3.3");
    doc["count"] = cv(2);
    ASSERT_TRUE(doc.is_object());
    ASSERT_EQ(doc.size(), 2U);
    EXPECT_EQ(doc.key_at(0), "version");
    EXPECT_EQ(doc.key_at(1), "count");
    EXPECT_EQ(doc.string_at("version"), "3.3");
    EXPECT_EQ(doc.int_at("count"), 2);
}

TEST(Config, SubscriptReusesAnExistingKeyAndKeepsItsPlace) {
    cv doc = cv::object({{"a", 1}, {"b", 2}});
    doc["a"] = cv(9);
    ASSERT_EQ(doc.size(), 2U);
    EXPECT_EQ(doc.key_at(0), "a");
    EXPECT_EQ(doc.int_at("a"), 9);
}

TEST(Config, SubscriptOnAScalarThrows) {
    cv scalar(7);
    EXPECT_THROW((void)scalar["k"], atp::config::access_error);
}

TEST(Config, EraseByKey) {
    cv doc;
    doc["gain"] = cv(0.5);
    EXPECT_EQ(doc.double_at("gain"), 0.5);
    EXPECT_TRUE(doc.erase("gain"));
    EXPECT_EQ(doc.size(), 0U);
    EXPECT_FALSE(doc.erase("gain"));
    EXPECT_FALSE(cv(7).erase("gain"));
}

TEST(Config, MutableFindHandsBackTheStoredNode) {
    cv doc = cv::object({{"nested", cv::object({{"a", 1}})}});
    cv* nested = doc.find("nested");
    ASSERT_NE(nested, nullptr);
    (*nested)["a"] = cv(2);
    EXPECT_EQ(doc.at("nested").int_at("a"), 2);
    EXPECT_EQ(doc.find("absent"), nullptr);
}

TEST(Config, PushBackBuildsAnArrayOutOfNull) {
    cv items;
    items.push_back(cv(1));
    items.push_back(cv("two"));
    ASSERT_TRUE(items.is_array());
    ASSERT_EQ(items.size(), 2U);
    EXPECT_EQ(items[0].as_int(), 1);
    EXPECT_EQ(items[1].as_string(), "two");
}

TEST(Config, PushBackOnAnObjectThrows) {
    cv doc = cv::object({{"a", 1}});
    EXPECT_THROW(doc.push_back(cv(1)), atp::config::access_error);
}

TEST(Config, MutableIndexReachesArrayElementsAndObjectValues) {
    cv items = cv::array({1, 2});
    items[std::size_t{0}] = cv(9);
    EXPECT_EQ(items[0].as_int(), 9);

    cv doc = cv::object({{"a", 1}});
    doc[std::size_t{0}] = cv(9);
    EXPECT_EQ(doc.int_at("a"), 9);

    cv scalar(7);
    EXPECT_THROW((void)scalar[std::size_t{0}], atp::config::access_error);
    EXPECT_THROW((void)items[std::size_t{5}], atp::config::access_error);
}

TEST(Config, EraseByIndex) {
    cv items = cv::array({1, 2, 3});
    EXPECT_TRUE(items.erase(std::size_t{1}));
    ASSERT_EQ(items.size(), 2U);
    EXPECT_EQ(items[1].as_int(), 3);
    EXPECT_FALSE(items.erase(std::size_t{5}));
    EXPECT_FALSE(cv(7).erase(std::size_t{0}));
}

TEST(Config, EntriesWalkAnObjectInOrder) {
    const cv doc = cv::object({{"b", 2}, {"a", 1}});
    std::vector<std::string> keys;
    std::vector<std::int64_t> values;
    for (const auto& [key, value] : doc.entries()) {
        keys.push_back(key);
        values.push_back(value.as_int());
    }
    EXPECT_EQ(keys, (std::vector<std::string>{"b", "a"}));
    EXPECT_EQ(values, (std::vector<std::int64_t>{2, 1}));
}

TEST(Config, ElementsWalkAnArrayInOrder) {
    const cv items = cv::array({1, 2, 3});
    std::int64_t sum = 0;
    for (const cv& item : items.elements()) {
        sum += item.as_int();
    }
    EXPECT_EQ(sum, 6);
}

TEST(Config, EntriesAndElementsAreEmptyForEveryOtherForm) {
    EXPECT_TRUE(cv(7).entries().empty());
    EXPECT_TRUE(cv(7).elements().empty());
    EXPECT_TRUE(cv::array({1}).entries().empty());
    EXPECT_TRUE(cv::object({{"a", 1}}).elements().empty());
    EXPECT_TRUE(cv().entries().empty());
    EXPECT_TRUE(cv().elements().empty());
}
