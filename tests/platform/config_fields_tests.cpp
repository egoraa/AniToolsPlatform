// SPDX-License-Identifier: Apache-2.0
#include <cstdint>
#include <deque>
#include <string>

#include <gtest/gtest.h>

#include <atp/config.hpp>
#include <atp/module.hpp>

namespace {

struct scaler_config : atp::config::fields {
    using fields::fields;
    double& gain = field("gain", 1.0);
    std::int64_t& taps = field("taps", std::int64_t{4});
    bool& invert = field("invert", false);
    std::string& preset = field("preset", "default");
    std::string& device = field<std::string>("device");
};

}  // namespace

TEST(ConfigFields, ReadsEveryScalarFormAndKeepsDeclarationOrder) {
    const atp::config::node source = atp::config::node::object(
        {{"gain", 2.5}, {"taps", 8}, {"invert", true}, {"preset", "loud"}, {"device", "hw:0"}});
    const scaler_config cfg{source};

    EXPECT_TRUE(cfg.problems().empty());
    EXPECT_DOUBLE_EQ(cfg.gain, 2.5);
    EXPECT_EQ(cfg.taps, 8);
    EXPECT_TRUE(cfg.invert);
    EXPECT_EQ(cfg.preset, "loud");
    EXPECT_EQ(cfg.device, "hw:0");

    ASSERT_EQ(cfg.declared().size(), 5u);
    EXPECT_EQ(cfg.declared()[0].name, "gain");
    EXPECT_EQ(cfg.declared()[4].name, "device");
}

TEST(ConfigFields, AnAbsentOptionalFieldTakesItsDefault) {
    const scaler_config cfg{atp::config::node::object({{"device", "hw:0"}})};

    EXPECT_TRUE(cfg.problems().empty());
    EXPECT_DOUBLE_EQ(cfg.gain, 1.0);
    EXPECT_EQ(cfg.taps, 4);
    EXPECT_FALSE(cfg.invert);
    EXPECT_EQ(cfg.preset, "default");
}

TEST(ConfigFields, AWholeNumberIsAcceptedForARealField) {
    const scaler_config cfg{atp::config::node::object({{"gain", 3}, {"device", "hw:0"}})};

    EXPECT_TRUE(cfg.problems().empty())
        << "nobody writes 3.0 in a config, and taking the default instead would be silent";
    EXPECT_DOUBLE_EQ(cfg.gain, 3.0);
}

TEST(ConfigFields, ARealIsNotAcceptedForAnIntegerFieldEvenWithoutAFraction) {
    const scaler_config cfg{atp::config::node::object({{"taps", 8.0}, {"device", "hw:0"}})};

    ASSERT_EQ(cfg.problems().size(), 1u);
    EXPECT_NE(cfg.problems()[0].find("taps"), std::string::npos);
    EXPECT_EQ(cfg.taps, 4);
}

TEST(ConfigFields, ANullValueIsAbsenceAndNotAWrongType) {
    const scaler_config cfg{atp::config::node::object({{"gain", atp::config::node{}}, {"device", "hw:0"}})};

    EXPECT_TRUE(cfg.problems().empty());
    EXPECT_DOUBLE_EQ(cfg.gain, 1.0);
}

TEST(ConfigFields, EveryProblemIsCollectedRatherThanTheFirstOne) {
    const scaler_config cfg{atp::config::node::object({{"gain", "loud"}, {"invert", 1}, {"nonsense", true}})};

    EXPECT_EQ(cfg.problems().size(), 4u) << "wrong gain, wrong invert, unknown key, missing device";
    std::string all;
    for (const std::string& p : cfg.problems()) {
        all += p;
        all += '\n';
    }
    EXPECT_NE(all.find("gain"), std::string::npos) << all;
    EXPECT_NE(all.find("invert"), std::string::npos) << all;
    EXPECT_NE(all.find("nonsense"), std::string::npos) << all;
    EXPECT_NE(all.find("device"), std::string::npos) << all;
}

TEST(ConfigFields, AskingTwiceDoesNotDoubleTheList) {
    const scaler_config cfg{atp::config::node::object({{"nonsense", true}})};

    const std::size_t first = cfg.problems().size();
    EXPECT_EQ(cfg.problems().size(), first);
}

TEST(ConfigFields, ThrowIfInvalidNamesTheFileAndEveryProblemAtOnce) {
    const atp::module_config whole(atp::config::node::object({{"gain", "loud"}}), "{}", "rig.json");
    const scaler_config cfg{whole};

    try {
        cfg.throw_if_invalid();
        FAIL() << "a config with problems must not pass";
    } catch (const atp::config::access_error& e) {
        const std::string text = e.what();
        EXPECT_NE(text.find("rig.json"), std::string::npos) << text;
        EXPECT_NE(text.find("gain"), std::string::npos) << text;
        EXPECT_NE(text.find("device"), std::string::npos) << text;
    }
}

TEST(ConfigFields, AConfigThatWasNeverGivenIsAllDefaultsAndOneMissingRequired) {
    const scaler_config cfg{atp::module_config{}};

    ASSERT_EQ(cfg.problems().size(), 1u) << "a null root declares nothing unknown; only device is missing";
    EXPECT_DOUBLE_EQ(cfg.gain, 1.0);
}

TEST(ConfigFields, TheSchemaIsReadableWithoutASource) {
    const scaler_config schema;

    ASSERT_EQ(schema.declared().size(), 5u);
    EXPECT_EQ(schema.declared()[0].name, "gain");
    EXPECT_EQ(schema.declared()[0].kind, atp::config::field_kind::real);
    EXPECT_FALSE(schema.declared()[0].required);
    EXPECT_DOUBLE_EQ(schema.declared()[0].default_value.as_double(), 1.0);
    EXPECT_TRUE(schema.declared()[4].required);
    EXPECT_TRUE(schema.declared()[4].default_value.is_null());
    EXPECT_TRUE(schema.problems().empty()) << "declaring a schema is not reading a config";
}

namespace {

struct channel_config : atp::config::fields {
    using fields::fields;
    std::int64_t& index = field("index", std::int64_t{0});
    double& rate = field("rate", 48000.0);
};

struct rig_config : atp::config::fields {
    using fields::fields;
    std::string& name = field("name", "rig");
    channel_config& master = group<channel_config>("master");
    std::deque<channel_config>& channels = list<channel_config>("channels");
    std::deque<std::string>& tags = list<std::string>("tags");
};

}  // namespace

TEST(ConfigFields, AGroupIsReadAsANestedObject) {
    const rig_config cfg{
        atp::config::node::object({{"master", atp::config::node::object({{"index", 2}, {"rate", 96000}})}})};

    EXPECT_TRUE(cfg.problems().empty());
    EXPECT_EQ(cfg.master.index, 2);
    EXPECT_DOUBLE_EQ(cfg.master.rate, 96000.0);
}

TEST(ConfigFields, AnAbsentGroupIsAllDefaultsAndNotAProblem) {
    const rig_config cfg{atp::config::node::object({{"name", "desk"}})};

    EXPECT_TRUE(cfg.problems().empty());
    EXPECT_EQ(cfg.master.index, 0);
    EXPECT_DOUBLE_EQ(cfg.master.rate, 48000.0);
}

TEST(ConfigFields, AListOfGroupsBuildsOneElementPerItem) {
    const rig_config cfg{atp::config::node::object(
        {{"channels", atp::config::node::array({atp::config::node::object({{"index", 0}}),
                                                atp::config::node::object({{"index", 1}, {"rate", 44100}})})}})};

    EXPECT_TRUE(cfg.problems().empty());
    ASSERT_EQ(cfg.channels.size(), 2u);
    EXPECT_EQ(cfg.channels[0].index, 0);
    EXPECT_DOUBLE_EQ(cfg.channels[0].rate, 48000.0);
    EXPECT_EQ(cfg.channels[1].index, 1);
    EXPECT_DOUBLE_EQ(cfg.channels[1].rate, 44100.0);
}

TEST(ConfigFields, AListOfScalarsIsReadAsItself) {
    const rig_config cfg{atp::config::node::object(
        {{"tags", atp::config::node::array({atp::config::node("live"), atp::config::node("stage")})}})};

    EXPECT_TRUE(cfg.problems().empty());
    ASSERT_EQ(cfg.tags.size(), 2u);
    EXPECT_EQ(cfg.tags[0], "live");
    EXPECT_EQ(cfg.tags[1], "stage");
}

TEST(ConfigFields, AProblemInsideAnElementIsReportedWithItsPath) {
    const rig_config cfg{atp::config::node::object(
        {{"channels", atp::config::node::array({atp::config::node::object({{"index", "first"}})})}})};

    ASSERT_EQ(cfg.problems().size(), 1u);
    EXPECT_NE(cfg.problems()[0].find("channels[0].index"), std::string::npos)
        << "a problem three levels down is useless without the path to it: " << cfg.problems()[0];
}

TEST(ConfigFields, AGroupThatIsNotAnObjectIsOneProblemAndNotACascade) {
    const rig_config cfg{atp::config::node::object({{"master", 5}})};

    ASSERT_EQ(cfg.problems().size(), 1u) << "reading a number as an object must not also report its fields";
    EXPECT_NE(cfg.problems()[0].find("master"), std::string::npos);
}

TEST(ConfigFields, TheSchemaOfANestedConfigCarriesItsChildren) {
    const rig_config schema;

    ASSERT_EQ(schema.declared().size(), 4u);
    EXPECT_EQ(schema.declared()[1].name, "master");
    EXPECT_EQ(schema.declared()[1].kind, atp::config::field_kind::object);
    ASSERT_EQ(schema.declared()[1].children.size(), 2u);
    EXPECT_EQ(schema.declared()[1].children[0].name, "index");

    EXPECT_EQ(schema.declared()[2].name, "channels");
    EXPECT_EQ(schema.declared()[2].kind, atp::config::field_kind::array);
    ASSERT_EQ(schema.declared()[2].children.size(), 2u)
        << "an array of groups declares the element's fields, since no element exists in a schema";

    EXPECT_EQ(schema.declared()[3].name, "tags");
    EXPECT_EQ(schema.declared()[3].kind, atp::config::field_kind::array);
    EXPECT_TRUE(schema.declared()[3].children.empty());
}
