// SPDX-License-Identifier: Apache-2.0
#include <atp/config/read.hpp>
#include <atp/module/module_config.hpp>

#include <gtest/gtest.h>

#include <string>

namespace {

atp::module_config sample() {
    return atp::module_config(atp::config::node::object(
        {{"test", atp::config::node::object({{"subblock", atp::config::node::object({{"value", 42}})}})},
         {"channels", atp::config::node::array({atp::config::node::object({{"rate", 44100}}),
                                                atp::config::node::object({{"rate", 48000}})})},
         {"matrix", atp::config::node::array({atp::config::node::array({1, 2}), atp::config::node::array({3, 4})})},
         {"count", 5},
         {"dotted.key", "reachable only through root()"}}));
}

}  // namespace

TEST(ModuleConfig, DefaultIsNullWithoutText) {
    const atp::module_config cfg;
    EXPECT_TRUE(cfg.root().is_null());
    EXPECT_TRUE(cfg.text().empty());
    EXPECT_TRUE(cfg.origin().empty());
    EXPECT_FALSE(cfg.is_opaque());
    EXPECT_FALSE(cfg.from_file());
}

TEST(ModuleConfig, TreeWithoutFileHasNoText) {
    const atp::module_config cfg(atp::config::node::object({{"gain", 3}}));
    EXPECT_EQ(cfg.root().int_at("gain"), 3);
    EXPECT_TRUE(cfg.text().empty());
    EXPECT_FALSE(cfg.is_opaque());
    EXPECT_FALSE(cfg.from_file());
}

TEST(ModuleConfig, ParsedFileKeepsBothTreeAndText) {
    const atp::module_config cfg(atp::config::node::object({{"gain", 3}}), "{\"gain\":3}", "rig.json");
    EXPECT_EQ(cfg.root().int_at("gain"), 3);
    EXPECT_EQ(cfg.text(), "{\"gain\":3}");
    EXPECT_EQ(cfg.origin(), "rig.json");
    EXPECT_FALSE(cfg.is_opaque());
    EXPECT_TRUE(cfg.from_file());
}

TEST(ModuleConfig, OpaqueIsTextWithoutTree) {
    const atp::module_config cfg = atp::module_config::opaque("rate = 48000\n", "rig.ini");
    EXPECT_TRUE(cfg.root().is_null());
    EXPECT_EQ(cfg.text(), "rate = 48000\n");
    EXPECT_EQ(cfg.origin(), "rig.ini");
    EXPECT_TRUE(cfg.is_opaque());
    EXPECT_TRUE(cfg.from_file());
}

TEST(ModuleConfig, OpaqueIsDistinguishableFromAJsonNull) {
    const atp::module_config parsed(atp::config::node{}, "null", "rig.json");
    const atp::module_config opaque = atp::module_config::opaque("null", "rig.ini");
    EXPECT_TRUE(parsed.root().is_null());
    EXPECT_FALSE(parsed.is_opaque());
    EXPECT_TRUE(opaque.is_opaque());
}

TEST(ModuleConfig, ScalarUtilitiesFallBackOnNullptr) {
    const atp::config::node value = atp::config::node::object({{"on", true}, {"name", "rig"}});
    EXPECT_TRUE(atp::config::bool_or(value.find("on"), false));
    EXPECT_FALSE(atp::config::bool_or(value.find("off"), false));
    EXPECT_EQ(atp::config::int_or(value.find("count"), 7), 7);
    EXPECT_EQ(atp::config::double_or(value.find("rate"), 0.5), 0.5);
    EXPECT_EQ(atp::config::string_or(value.find("name"), "none"), "rig");
    EXPECT_EQ(atp::config::string_or(value.find("nope"), "none"), "none");
}

TEST(ModuleConfig, ScalarUtilitiesFallBackOnAnotherForm) {
    const atp::config::node value = atp::config::node::object({{"count", "five"}});
    EXPECT_EQ(atp::config::int_or(value.find("count"), 7), 7);
}

TEST(ModuleConfigPath, WalksObjects) {
    const atp::module_config cfg = sample();
    EXPECT_EQ(cfg.at("test.subblock.value").as_int(), 42);
}

TEST(ModuleConfigPath, WalksAnIndex) {
    const atp::module_config cfg = sample();
    EXPECT_EQ(cfg.at("channels[1].rate").as_int(), 48000);
}

TEST(ModuleConfigPath, WalksChainedIndices) {
    const atp::module_config cfg = sample();
    EXPECT_EQ(cfg.at("matrix[1][0]").as_int(), 3);
}

TEST(ModuleConfigPath, WalksALeadingIndexWhenTheRootIsAnArray) {
    const atp::module_config cfg(atp::config::node::array({atp::config::node::object({{"rate", 8000}})}));
    EXPECT_EQ(cfg.at("[0].rate").as_int(), 8000);
}

TEST(ModuleConfigPath, ContainsAnswersWithoutThrowing) {
    const atp::module_config cfg = sample();
    EXPECT_TRUE(cfg.contains("test.subblock.value"));
    EXPECT_FALSE(cfg.contains("test.subblock.other"));
}

TEST(ModuleConfigPath, FindAnswersNullptrForWhatIsNotThere) {
    const atp::module_config cfg = sample();
    EXPECT_EQ(cfg.find("test.subblock.other"), nullptr);
    EXPECT_EQ(cfg.find("channels[9].rate"), nullptr);
    EXPECT_EQ(cfg.find("count.deeper"), nullptr);
}

TEST(ModuleConfigPath, DefaultsNeedNoCheckOnAnEmptyConfig) {
    const atp::module_config empty;
    EXPECT_EQ(atp::config::int_or(empty.find("audio.rate"), 48000), 48000);
}

TEST(ModuleConfigPath, AtNamesTheMissingKeyAndTheContainer) {
    const atp::module_config cfg = sample();
    try {
        (void)cfg.at("test.subblock.other");
        FAIL();
    } catch (const atp::config::access_error& e) {
        EXPECT_STREQ(e.what(), "config: 'test.subblock.other' has no key 'other' in 'test.subblock'");
    }
}

TEST(ModuleConfigPath, AtNamesTheIndexAndTheSize) {
    const atp::module_config cfg = sample();
    try {
        (void)cfg.at("channels[9].rate");
        FAIL();
    } catch (const atp::config::access_error& e) {
        EXPECT_STREQ(e.what(), "config: 'channels[9].rate' has no index 9 in 'channels' (size 2)");
    }
}

TEST(ModuleConfigPath, AtNamesTheFormItStoppedOn) {
    const atp::module_config cfg = sample();
    try {
        (void)cfg.at("count.deeper");
        FAIL();
    } catch (const atp::config::access_error& e) {
        EXPECT_STREQ(e.what(), "config: 'count.deeper' stops at 'count' (integer)");
    }
}

TEST(ModuleConfigPath, AtNamesTheRootWhenNothingResolved) {
    const atp::module_config empty;
    try {
        (void)empty.at("a.b");
        FAIL();
    } catch (const atp::config::access_error& e) {
        EXPECT_STREQ(e.what(), "config: 'a.b' stops at the root (null)");
    }
}

TEST(ModuleConfigPath, BadGrammarThrowsEvenFromFind) {
    const atp::module_config cfg = sample();
    EXPECT_THROW((void)cfg.find(""), atp::config::access_error);
    EXPECT_THROW((void)cfg.find("a..b"), atp::config::access_error);
    EXPECT_THROW((void)cfg.find("a."), atp::config::access_error);
    EXPECT_THROW((void)cfg.find("a.[0]"), atp::config::access_error);
    EXPECT_THROW((void)cfg.find("channels[]"), atp::config::access_error);
    EXPECT_THROW((void)cfg.find("channels[x]"), atp::config::access_error);
    EXPECT_THROW((void)cfg.find("channels[1"), atp::config::access_error);
    EXPECT_THROW((void)cfg.find("channels[1]junk"), atp::config::access_error);
    EXPECT_THROW((void)cfg.find("channels[99999999999999999999]"), atp::config::access_error);
}

TEST(ModuleConfigPath, BadGrammarThrowsEvenAfterAKeyThatIsNotThere) {
    const atp::module_config cfg = sample();
    EXPECT_THROW((void)cfg.find("absent..b"), atp::config::access_error);
    EXPECT_THROW((void)cfg.find("absent[1"), atp::config::access_error);
    EXPECT_THROW((void)cfg.find("absent.deeper."), atp::config::access_error);
}

TEST(ModuleConfigPath, BadGrammarNamesTheOffset) {
    const atp::module_config cfg = sample();
    try {
        (void)cfg.find("channels[1");
        FAIL();
    } catch (const atp::config::access_error& e) {
        EXPECT_STREQ(e.what(), "config: bad path 'channels[1' (unclosed '[' at 8)");
    }
}

TEST(ModuleConfigPath, AKeyWithADotIsReachableOnlyThroughRoot) {
    const atp::module_config cfg = sample();
    EXPECT_EQ(cfg.find("dotted.key"), nullptr);
    EXPECT_EQ(cfg.root().string_at("dotted.key"), "reachable only through root()");
}
