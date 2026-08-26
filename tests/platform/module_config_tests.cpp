// SPDX-License-Identifier: Apache-2.0
#include <array>
#include <cstdint>
#include <deque>
#include <span>
#include <string>

#include <gtest/gtest.h>

#include <atp/io/enum_names.hpp>
#include <atp/module/module_config.hpp>

namespace {

enum class channel_layout { mono, stereo, surround };

enum class dither { off, triangular };

}  // namespace

template <>
struct atp::io::enum_names<channel_layout> {
    static constexpr std::array entries{
        atp::io::enum_entry{channel_layout::mono, "mono"},
        atp::io::enum_entry{channel_layout::stereo, "stereo"},
        atp::io::enum_entry{channel_layout::surround, "surround"},
    };
};

template <>
struct atp::io::enum_names<dither> {
    static constexpr std::array entries{
        atp::io::enum_entry{dither::off, "off"},
        atp::io::enum_entry{dither::triangular, "triangular"},
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
    channel_layout& narrowed =
        field("narrowed", channel_layout::mono, atp::io::allowed(channel_layout::mono, channel_layout::stereo));
    dither& noise = field<dither>("noise");
    std::string& label = field("label", "main");
    std::deque<channel_layout>& busses = list<channel_layout>("busses");
};

}  // namespace

TEST(ModuleConfig, AFreshObjectHoldsTheDeclaredDefaults) {
    const rig cfg;

    EXPECT_DOUBLE_EQ(cfg.gain, 1.0);
    EXPECT_EQ(cfg.taps, 4);
    EXPECT_FALSE(cfg.invert);
    EXPECT_EQ(cfg.preset, "default");
    EXPECT_EQ(cfg.device, "");
    EXPECT_EQ(cfg.sound.rate, 48000);
    EXPECT_TRUE(cfg.bands.empty());
    EXPECT_TRUE(cfg.tags.empty());
}

TEST(ModuleConfig, EntriesComeInDeclarationOrder) {
    const rig cfg;
    const std::span<const atp::module_config::entry> e = cfg.entries();

    ASSERT_EQ(e.size(), 8u);
    EXPECT_EQ(e[0].name(), "gain");
    EXPECT_EQ(e[4].name(), "device");
    EXPECT_EQ(e[5].name(), "audio");
    EXPECT_EQ(e[6].name(), "bands");
    EXPECT_EQ(e[7].name(), "tags");
}

TEST(ModuleConfig, AnEntryReportsItsKindAndWhetherItIsRequired) {
    const rig cfg;

    EXPECT_EQ(cfg.find("gain")->kind(), atp::field_kind::real);
    EXPECT_EQ(cfg.find("taps")->kind(), atp::field_kind::integer);
    EXPECT_EQ(cfg.find("invert")->kind(), atp::field_kind::boolean);
    EXPECT_EQ(cfg.find("preset")->kind(), atp::field_kind::string);
    EXPECT_EQ(cfg.find("audio")->kind(), atp::field_kind::object);
    EXPECT_EQ(cfg.find("bands")->kind(), atp::field_kind::array);
    EXPECT_EQ(cfg.find("tags")->element(), atp::field_kind::string);
    EXPECT_EQ(cfg.find("bands")->element(), atp::field_kind::object);

    EXPECT_FALSE(cfg.find("gain")->required());
    EXPECT_TRUE(cfg.find("device")->required());
}

TEST(ModuleConfig, WritingThroughAnEntryReachesTheBoundReference) {
    rig cfg;
    cfg.find("gain")->set(2.5);

    EXPECT_DOUBLE_EQ(cfg.gain, 2.5);
    EXPECT_DOUBLE_EQ(cfg.find("gain")->value<double>(), 2.5);
}

TEST(ModuleConfig, ReadingAnEntryAsTheWrongFormThrows) {
    const rig cfg;

    EXPECT_THROW((void)cfg.find("gain")->value<std::string>(), atp::config::access_error);
}

TEST(ModuleConfig, TheStringLayerMatchesTheDeclaredDefault) {
    rig cfg;

    EXPECT_EQ(cfg.find("gain")->default_string(), "1");
    EXPECT_EQ(cfg.find("gain")->to_string(), "1");
    EXPECT_TRUE(cfg.find("gain")->is_default());

    EXPECT_TRUE(cfg.find("gain")->from_string("2.5"));
    EXPECT_DOUBLE_EQ(cfg.gain, 2.5);
    EXPECT_FALSE(cfg.find("gain")->is_default());

    EXPECT_FALSE(cfg.find("taps")->from_string("not a number"));
    EXPECT_EQ(cfg.taps, 4) << "a string that does not parse must leave the value alone";
}

TEST(ModuleConfig, AFieldKnowsWhetherAnybodyWroteIt) {
    rig cfg;

    EXPECT_FALSE(cfg.find("device")->is_set());
    cfg.find("device")->set(std::string("hw:0"));
    EXPECT_TRUE(cfg.find("device")->is_set());

    cfg.gain = 2.0;
    EXPECT_FALSE(cfg.find("gain")->is_set()) << "a write through the bound reference is not a write through an entry";
}

TEST(ModuleConfig, AFieldWithNoDeclaredDefaultIsNeverAtIt) {
    const rig cfg;

    EXPECT_FALSE(cfg.find("device")->is_default()) << "a required field has no default to be at";
    EXPECT_FALSE(cfg.find("audio")->is_default());
    EXPECT_FALSE(cfg.find("bands")->is_default()) << "an object and an array are described by their contents";
}

TEST(ModuleConfig, AGroupIsReachedAsAConfigOfItsOwn) {
    rig cfg;
    cfg.find("audio")->group().find("rate")->set(std::int64_t{44100});

    EXPECT_EQ(cfg.sound.rate, 44100);
}

TEST(ModuleConfig, ResizingAListOfObjectsGrowsAndShrinksIt) {
    rig cfg;
    atp::module_config::entry& bands = *cfg.find("bands");

    bands.resize(2);
    ASSERT_EQ(cfg.bands.size(), 2u);
    EXPECT_DOUBLE_EQ(cfg.bands[0].low, 20.0) << "a fresh element holds its declared defaults";

    bands.group_at(1).find("low")->set(100.0);
    EXPECT_DOUBLE_EQ(cfg.bands[1].low, 100.0);

    bands.resize(1);
    EXPECT_EQ(cfg.bands.size(), 1u);
    EXPECT_DOUBLE_EQ(cfg.bands[0].low, 20.0) << "shrinking drops the tail, not the head";
}

TEST(ModuleConfig, AListOfObjectsIsNotReadableAsAListOfScalars) {
    const rig cfg;

    EXPECT_THROW((void)cfg.find("bands")->values<std::string>(), atp::config::access_error);
    EXPECT_THROW((void)cfg.find("bands")->element_string(0), atp::config::access_error);
}

TEST(ModuleConfig, ReachingPastTheEndOfAListThrows) {
    rig cfg;
    atp::module_config::entry& bands = *cfg.find("bands");
    atp::module_config::entry& tags = *cfg.find("tags");

    bands.resize(1);
    EXPECT_THROW((void)bands.group_at(1), atp::config::access_error);

    tags.resize(2);
    EXPECT_THROW((void)tags.element_string(2), atp::config::access_error);
    EXPECT_THROW(tags.set_element_from_string(2, "spare"), atp::config::access_error);
}

TEST(ModuleConfig, AnEmptyListStillDescribesItsElement) {
    const rig cfg;
    const atp::module_config& shape = cfg.find("bands")->element_shape();

    ASSERT_EQ(shape.entries().size(), 2u);
    EXPECT_EQ(shape.entries()[0].name(), "low");
}

TEST(ModuleConfig, AListOfScalarsIsEditedTypedAndAsText) {
    rig cfg;
    atp::module_config::entry& tags = *cfg.find("tags");

    tags.resize(2);
    ASSERT_EQ(cfg.tags.size(), 2u);
    EXPECT_EQ(cfg.tags[0], "") << "a fresh element starts at the zero of its kind";

    tags.values<std::string>()[0] = "left";
    EXPECT_TRUE(tags.set_element_from_string(1, "right"));
    EXPECT_EQ(cfg.tags[1], "right");
    EXPECT_EQ(tags.element_string(0), "left");
}

TEST(ModuleConfig, ASourceCanBeAttachedAndReadBack) {
    rig cfg;
    cfg.attach_source("rate = 48000\n", "rig.ini", true);

    EXPECT_EQ(cfg.text(), "rate = 48000\n");
    EXPECT_EQ(cfg.origin(), "rig.ini");
    EXPECT_TRUE(cfg.is_opaque());
}

TEST(ModuleConfig, ABareBaseIsAUsableConfigWithNoFields) {
    atp::module_config cfg;
    cfg.attach_source("x", "rig.ini", true);

    EXPECT_TRUE(cfg.entries().empty());
    EXPECT_EQ(cfg.text(), "x");
}

TEST(ModuleConfig, AnEnumFieldBindsTheEnumItself) {
    mixer cfg;

    EXPECT_EQ(cfg.layout, channel_layout::stereo) << "the declared default, as the enum and not as its name";
    cfg.find("layout")->set(channel_layout::surround);
    EXPECT_EQ(cfg.layout, channel_layout::surround);
    EXPECT_EQ(cfg.find("layout")->value<channel_layout>(), channel_layout::surround);
}

TEST(ModuleConfig, AnEnumFieldIsAStringWithOptions) {
    const mixer cfg;

    EXPECT_EQ(cfg.find("layout")->kind(), atp::field_kind::string)
        << "one of a set is expressed by options(), not by a kind of its own: the document holds a name";
    EXPECT_EQ(cfg.find("layout")->options().size(), 3u);
    EXPECT_EQ(cfg.find("layout")->options()[0], "mono") << "in declaration order, as the name table was written";
    EXPECT_TRUE(cfg.find("label")->options().empty()) << "an ordinary string field constrains nothing";
}

TEST(ModuleConfig, AnEnumFieldReadsAndWritesItsName) {
    mixer cfg;

    EXPECT_EQ(cfg.find("layout")->to_string(), "stereo");
    EXPECT_EQ(cfg.find("layout")->default_string(), "stereo");
    EXPECT_TRUE(cfg.find("layout")->is_default());

    EXPECT_TRUE(cfg.find("layout")->from_string("mono"));
    EXPECT_EQ(cfg.layout, channel_layout::mono);
    EXPECT_FALSE(cfg.find("layout")->is_default());
}

TEST(ModuleConfig, ANameOutsideTheSetIsRefusedAndChangesNothing) {
    mixer cfg;

    EXPECT_FALSE(cfg.find("layout")->from_string("quadraphonic"));
    EXPECT_EQ(cfg.layout, channel_layout::stereo) << "a name nobody declared leaves the value alone";

    EXPECT_FALSE(cfg.find("narrowed")->from_string("surround"))
        << "the listed set replaces the type's table, which is how a module says what it supports";
    EXPECT_EQ(cfg.narrowed, channel_layout::mono);
    EXPECT_EQ(cfg.find("narrowed")->options().size(), 2u);
}

TEST(ModuleConfig, AnEnumFieldOfAnotherEnumsTypeIsRefused) {
    mixer cfg;

    EXPECT_THROW(cfg.find("layout")->set(dither::off), atp::config::access_error)
        << "two enums share the string kind, so the kind alone cannot tell them apart";
    EXPECT_THROW((void)cfg.find("layout")->value<std::string>(), atp::config::access_error)
        << "and a name is not what the field holds either";
    EXPECT_EQ(cfg.layout, channel_layout::stereo);
}

TEST(ModuleConfig, ARequiredEnumHasNoDefaultToBeAt) {
    const mixer cfg;

    EXPECT_TRUE(cfg.find("noise")->required());
    EXPECT_TRUE(cfg.find("noise")->default_string().empty());
    EXPECT_FALSE(cfg.find("noise")->is_default());
    EXPECT_FALSE(cfg.find("noise")->is_set());
}

TEST(ModuleConfig, AnArrayOfEnumsIsSizedAndWrittenByName) {
    mixer cfg;

    EXPECT_EQ(cfg.find("busses")->element(), atp::field_kind::string);
    cfg.find("busses")->resize(2);
    ASSERT_EQ(cfg.busses.size(), 2u);
    EXPECT_EQ(cfg.busses[0], channel_layout::mono) << "a fresh element is the zero of its enum";

    EXPECT_TRUE(cfg.find("busses")->set_element_from_string(1, "surround"));
    EXPECT_EQ(cfg.busses[1], channel_layout::surround);
    EXPECT_EQ(cfg.find("busses")->element_string(1), "surround");

    EXPECT_FALSE(cfg.find("busses")->set_element_from_string(0, "quadraphonic"));
    EXPECT_EQ(cfg.busses[0], channel_layout::mono);
}
