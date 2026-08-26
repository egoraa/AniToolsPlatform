// SPDX-License-Identifier: Apache-2.0
#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <atp/io/enum_names.hpp>
#include <atp/module/dynamic_config.hpp>
#include <atp/module/module_config.hpp>
#include <atp/runtime/config_binding.hpp>
#include <atp/runtime/config_error.hpp>
#include <atp/runtime/config_source.hpp>

namespace {

enum class channel_layout { mono, stereo, surround };

}  // namespace

template <>
struct atp::io::enum_names<channel_layout> {
    static constexpr std::array entries{
        atp::io::enum_entry{channel_layout::mono, "mono"},
        atp::io::enum_entry{channel_layout::stereo, "stereo"},
        atp::io::enum_entry{channel_layout::surround, "surround"},
    };
};

namespace {

struct band : atp::module_config {
    using module_config::module_config;
    double& low = field("low", 20.0);
    double& high = field("high", 20000.0);
};

struct audio : atp::module_config {
    using module_config::module_config;
    std::int64_t& rate = field("rate", std::int64_t{48000});
};

struct rig : atp::module_config {
    using module_config::module_config;
    double& gain = field("gain", 1.0);
    std::int64_t& taps = field("taps", std::int64_t{4});
    bool& invert = field("invert", false);
    std::string& preset = field("preset", "default");
    std::string& device = field<std::string>("device");
    audio& sound = group<audio>("audio");
    std::deque<band>& bands = list<band>("bands");
    std::deque<std::string>& tags = list<std::string>("tags");
};

struct mixer : atp::module_config {
    using module_config::module_config;
    channel_layout& layout = field("layout", channel_layout::stereo);
    std::deque<channel_layout>& busses = list<channel_layout>("busses");
};

}  // namespace

TEST(ConfigBinding, ReadsEveryScalarForm) {
    rig cfg;
    const atp::config::node doc = atp::config::node::object(
        {{"gain", 2.5}, {"taps", 8}, {"invert", true}, {"preset", "loud"}, {"device", "hw:0"}});

    EXPECT_TRUE(atp::runtime::load_fields(cfg, {doc, {}, {}, false}).empty());
    EXPECT_DOUBLE_EQ(cfg.gain, 2.5);
    EXPECT_EQ(cfg.taps, 8);
    EXPECT_TRUE(cfg.invert);
    EXPECT_EQ(cfg.preset, "loud");
    EXPECT_EQ(cfg.device, "hw:0");
}

TEST(ConfigBinding, AnAbsentOptionalFieldKeepsItsDefault) {
    rig cfg;
    const atp::config::node doc = atp::config::node::object({{"device", "hw:0"}});

    EXPECT_TRUE(atp::runtime::load_fields(cfg, {doc, {}, {}, false}).empty());
    EXPECT_DOUBLE_EQ(cfg.gain, 1.0);
    EXPECT_FALSE(cfg.find("gain")->is_set());
}

TEST(ConfigBinding, AWholeNumberWidensIntoARealField) {
    rig cfg;
    const atp::config::node doc = atp::config::node::object({{"gain", 3}, {"device", "hw:0"}});

    EXPECT_TRUE(atp::runtime::load_fields(cfg, {doc, {}, {}, false}).empty())
        << "nobody writes 3.0 in a config, and taking the default instead would be silent";
    EXPECT_DOUBLE_EQ(cfg.gain, 3.0);
}

TEST(ConfigBinding, ARealIsRefusedForAnIntegerFieldEvenWithoutAFraction) {
    rig cfg;
    const atp::config::node doc = atp::config::node::object({{"taps", 8.0}, {"device", "hw:0"}});

    const std::vector<std::string> problems = atp::runtime::load_fields(cfg, {doc, {}, {}, false});
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_EQ(problems[0], "taps: expected integer, found real");
    EXPECT_EQ(cfg.taps, 4);
}

TEST(ConfigBinding, AMissingRequiredFieldIsAProblem) {
    rig cfg;
    const std::vector<std::string> problems =
        atp::runtime::load_fields(cfg, {atp::config::node(atp::config::node::object_type{}), {}, {}, false});

    ASSERT_EQ(problems.size(), 1u);
    EXPECT_EQ(problems[0], "device: required and absent");
}

TEST(ConfigBinding, AnUndeclaredKeyIsAProblem) {
    rig cfg;
    const atp::config::node doc = atp::config::node::object({{"device", "hw:0"}, {"gian", 2.0}});

    const std::vector<std::string> problems = atp::runtime::load_fields(cfg, {doc, {}, {}, false});
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_EQ(problems[0], "gian: not a field of this config");
}

TEST(ConfigBinding, AProblemInsideAGroupCarriesThePath) {
    rig cfg;
    const atp::config::node doc =
        atp::config::node::object({{"device", "hw:0"}, {"audio", atp::config::node::object({{"rate", "fast"}})}});

    const std::vector<std::string> problems = atp::runtime::load_fields(cfg, {doc, {}, {}, false});
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_EQ(problems[0], "audio.rate: expected integer, found string");
}

TEST(ConfigBinding, AListOfObjectsIsReadElementByElement) {
    rig cfg;
    const atp::config::node doc = atp::config::node::object(
        {{"device", "hw:0"},
         {"bands", atp::config::node::array(
                       {atp::config::node::object({{"low", 30.0}}), atp::config::node::object({{"high", 900.0}})})}});

    EXPECT_TRUE(atp::runtime::load_fields(cfg, {doc, {}, {}, false}).empty());
    ASSERT_EQ(cfg.bands.size(), 2u);
    EXPECT_DOUBLE_EQ(cfg.bands[0].low, 30.0);
    EXPECT_DOUBLE_EQ(cfg.bands[0].high, 20000.0);
    EXPECT_DOUBLE_EQ(cfg.bands[1].high, 900.0);
}

TEST(ConfigBinding, ABadElementIsOneProblemNamingItsIndex) {
    rig cfg;
    const atp::config::node doc = atp::config::node::object(
        {{"device", "hw:0"}, {"bands", atp::config::node::array({atp::config::node(std::string("loud"))})}});

    const std::vector<std::string> problems = atp::runtime::load_fields(cfg, {doc, {}, {}, false});
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_EQ(problems[0], "bands[0]: expected object, found string");
}

TEST(ConfigBinding, TheSourceIsAttachedEvenWhenNoFieldWasFilled) {
    rig cfg;
    (void)atp::runtime::load_fields(cfg, {{}, "rate = 48000\n", "rig.ini", true});

    EXPECT_EQ(cfg.text(), "rate = 48000\n");
    EXPECT_EQ(cfg.origin(), "rig.ini");
    EXPECT_TRUE(cfg.is_opaque());
}

TEST(ConfigBinding, ThrowingNamesTheFileAndEveryProblemAtOnce) {
    rig cfg;
    const atp::config::node doc = atp::config::node::object({{"taps", 8.0}, {"gian", 1}});

    try {
        atp::runtime::load_fields_or_throw(cfg, {doc, {}, "rig.json", false});
        FAIL() << "a config with problems must not pass";
    } catch (const atp::runtime::config_error& e) {
        const std::string text = e.what();
        EXPECT_NE(text.find("rig.json"), std::string::npos);
        EXPECT_NE(text.find("taps"), std::string::npos);
        EXPECT_NE(text.find("device"), std::string::npos);
        EXPECT_NE(text.find("gian"), std::string::npos);
    }
}

TEST(ConfigBinding, SavingWritesWhatDiffersFromTheDefault) {
    rig cfg;
    cfg.find("gain")->set(2.5);
    cfg.find("device")->set(std::string("hw:0"));

    const atp::config::node written = atp::runtime::save_fields(cfg);

    EXPECT_EQ(written.entries().size(), 2u);
    EXPECT_DOUBLE_EQ(written.find("gain")->as_double(), 2.5);
    EXPECT_EQ(written.find("device")->as_string(), "hw:0");
}

TEST(ConfigBinding, SavingDropsAFieldWrittenBackToItsDefault) {
    rig cfg;
    cfg.find("gain")->set(1.0);

    EXPECT_EQ(atp::runtime::save_fields(cfg).entries().size(), 0u);
}

TEST(ConfigBinding, SavingKeepsARequiredFieldEvenAtTheZeroOfItsType) {
    rig cfg;
    cfg.find("device")->set(std::string());

    const atp::config::node written = atp::runtime::save_fields(cfg);
    ASSERT_NE(written.find("device"), nullptr);
    EXPECT_EQ(written.find("device")->as_string(), "");
}

TEST(ConfigBinding, SavingKeepsTheLengthOfAListAndThinsItsElements) {
    rig cfg;
    cfg.find("bands")->resize(2);
    cfg.find("bands")->group_at(1).find("low")->set(100.0);

    const atp::config::node written = atp::runtime::save_fields(cfg);
    const atp::config::node* bands = written.find("bands");
    ASSERT_NE(bands, nullptr);
    ASSERT_EQ(bands->size(), 2u);
    EXPECT_EQ((*bands)[0].size(), 0u) << "an element at its defaults is {} and the position is the data";
    EXPECT_DOUBLE_EQ((*bands)[1].find("low")->as_double(), 100.0);
}

TEST(ConfigBinding, LoadingWhatWasSavedGivesTheSameDocument) {
    rig first;
    first.find("gain")->set(2.5);
    first.find("device")->set(std::string("hw:0"));
    first.find("bands")->resize(1);
    first.find("bands")->group_at(0).find("low")->set(30.0);
    const atp::config::node once = atp::runtime::save_fields(first);

    rig second;
    EXPECT_TRUE(atp::runtime::load_fields(second, {once, {}, {}, false}).empty());
    EXPECT_EQ(atp::runtime::save_fields(second), once);
}

TEST(ConfigBinding, AnEnumFieldIsReadFromItsName) {
    mixer cfg;
    const atp::config::node doc = atp::config::node::object({{"layout", "surround"}});

    EXPECT_TRUE(atp::runtime::load_fields(cfg, {doc, {}, {}, false}).empty());
    EXPECT_EQ(cfg.layout, channel_layout::surround);
    EXPECT_TRUE(cfg.find("layout")->is_set());
}

TEST(ConfigBinding, ANameOutsideTheSetIsOneProblemAndKeepsTheDefault) {
    mixer cfg;
    const atp::config::node doc = atp::config::node::object({{"layout", "quadraphonic"}});

    const std::vector<std::string> problems = atp::runtime::load_fields(cfg, {doc, {}, {}, false});
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_EQ(problems[0], "layout: 'quadraphonic' is not one of mono|stereo|surround")
        << "the reader has to be told which names exist, or the message is useless";
    EXPECT_EQ(cfg.layout, channel_layout::stereo);
    EXPECT_FALSE(cfg.find("layout")->is_set()) << "a refused name is not a value anybody wrote";
}

TEST(ConfigBinding, AnEnumFieldGivenSomethingOtherThanAStringIsAMismatch) {
    mixer cfg;
    const atp::config::node doc = atp::config::node::object({{"layout", 2}});

    const std::vector<std::string> problems = atp::runtime::load_fields(cfg, {doc, {}, {}, false});
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_EQ(problems[0], "layout: expected string, found integer") << "in a document an enumeration is a name";
}

TEST(ConfigBinding, AnArrayOfEnumsIsReadAndWrittenByName) {
    mixer cfg;
    const atp::config::node doc =
        atp::config::node::object({{"busses", atp::config::node::array({"mono", "surround"})}});

    EXPECT_TRUE(atp::runtime::load_fields(cfg, {doc, {}, {}, false}).empty());
    ASSERT_EQ(cfg.busses.size(), 2u);
    EXPECT_EQ(cfg.busses[0], channel_layout::mono);
    EXPECT_EQ(cfg.busses[1], channel_layout::surround);
}

TEST(ConfigBinding, ABadElementOfAnArrayOfEnumsNamesItsIndex) {
    mixer cfg;
    const atp::config::node doc =
        atp::config::node::object({{"busses", atp::config::node::array({"mono", "quadraphonic"})}});

    const std::vector<std::string> problems = atp::runtime::load_fields(cfg, {doc, {}, {}, false});
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_EQ(problems[0], "busses[1]: 'quadraphonic' is not one of mono|stereo|surround");
    EXPECT_EQ(cfg.busses[0], channel_layout::mono) << "a bad element is one problem, not a reason to stop";
    EXPECT_EQ(cfg.busses[1], channel_layout::mono) << "and the position stays, holding the zero it was grown at";
}

TEST(ConfigBinding, SavingAnEnumWritesItsNameBack) {
    mixer cfg;
    const atp::config::node doc =
        atp::config::node::object({{"layout", "mono"}, {"busses", atp::config::node::array({"surround"})}});

    EXPECT_TRUE(atp::runtime::load_fields(cfg, {doc, {}, {}, false}).empty());
    EXPECT_EQ(atp::runtime::save_fields(cfg), doc) << "what was loaded is what is saved, name for name";
}

TEST(ConfigBinding, ValuesOfKeepsDefaultsWhereSaveFieldsDropsThem) {
    atp::dynamic_config cfg;
    cfg.scalar("gain", atp::field_kind::real, "1.0");
    cfg.scalar("preset", atp::field_kind::string, "flat");

    const atp::config::node saved = atp::runtime::save_fields(cfg);
    EXPECT_TRUE(saved.entries().empty());

    const atp::config::node values = atp::runtime::values_of(cfg);
    ASSERT_EQ(values.entries().size(), 2U);
    EXPECT_DOUBLE_EQ(values.at("gain").as_double(), 1.0);
    EXPECT_EQ(values.at("preset").as_string(), "flat");
}

TEST(ConfigBinding, ValuesOfWritesTheDefaultsOfAnUntouchedGroup) {
    atp::dynamic_config cfg;
    cfg.object("master").scalar("gain", atp::field_kind::real, "1.0");

    const atp::config::node values = atp::runtime::values_of(cfg);
    ASSERT_TRUE(values.at("master").is_object());
    EXPECT_DOUBLE_EQ(values.at("master").at("gain").as_double(), 1.0);
}

TEST(ConfigBinding, ValuesOfWritesAnEmptyArrayForAnUngrownList) {
    atp::dynamic_config cfg;
    cfg.scalar_list("taps", atp::field_kind::real);

    const atp::config::node values = atp::runtime::values_of(cfg);
    ASSERT_TRUE(values.at("taps").is_array());
    EXPECT_EQ(values.at("taps").size(), 0U);
}

TEST(ConfigBinding, ValuesOfWritesEveryElementOfAnArrayOfObjects) {
    atp::dynamic_config cfg;
    cfg.object_list("voices", [](atp::dynamic_config& v) { v.scalar("note", atp::field_kind::integer, "60"); });
    cfg.find("voices")->resize(2);
    cfg.find("voices")->group_at(1).find("note")->set<std::int64_t>(72);

    const atp::config::node values = atp::runtime::values_of(cfg);
    ASSERT_EQ(values.at("voices").size(), 2U);
    EXPECT_EQ(values.at("voices")[0].at("note").as_int(), 60);
    EXPECT_EQ(values.at("voices")[1].at("note").as_int(), 72);
}

TEST(ConfigBinding, ValuesOfWritesTheZeroOfAFormForARequiredFieldNobodyFilled) {
    atp::dynamic_config cfg;
    cfg.required_scalar("rate", atp::field_kind::integer);

    const atp::config::node values = atp::runtime::values_of(cfg);
    EXPECT_EQ(values.at("rate").as_int(), 0);
}
