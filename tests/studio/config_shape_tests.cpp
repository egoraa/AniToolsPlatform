// SPDX-License-Identifier: Apache-2.0
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <atp/config.hpp>
#include <atp/config/node.hpp>
#include <atp/runtime/json_codec.hpp>
#include <atp/studio/config_shape.hpp>

namespace {

using cv = atp::config::node;

cv empty_object() {
    return cv(cv::object_type{});
}

struct band : atp::config::fields {
    using fields::fields;
    std::int64_t& upto = field("upto", std::int64_t{0});
    std::string& name = field("name", "?");
};

struct demo_config : atp::config::fields {
    using fields::fields;
    double& gain = field("gain", 1.0);
    bool& invert = field("invert", false);
    std::string& device = field<std::string>("device");
    std::int64_t& channel = field<std::int64_t>("channel");
    band& master = group<band>("master");
    std::deque<band>& bands = list<band>("bands");
    std::deque<std::string>& tags = list<std::string>("tags");
};

[[nodiscard]] std::vector<atp::config::field_declaration> schema() {
    const demo_config probe;
    return probe.declared();
}

}  // namespace

TEST(ConfigShape, MaterialiseFillsEveryDeclaredFieldFromItsDefault) {
    const cv full = atp::studio::materialise(schema(), empty_object());

    EXPECT_DOUBLE_EQ(full.at("gain").as_double(), 1.0);
    EXPECT_EQ(full.at("invert"), cv(false));
    EXPECT_NE(full.find("device"), nullptr) << "a required field appears too, so the tree can show it empty";
    EXPECT_TRUE(full.at("master").is_object());
    EXPECT_EQ(full.at("master").at("upto"), cv(0));
    EXPECT_TRUE(full.at("bands").is_array()) << "an absent list is an empty array, not a missing key";
    EXPECT_EQ(full.at("bands").size(), 0u);
}

TEST(ConfigShape, MaterialisePrefersWhatTheDocumentSays) {
    const cv stored = cv::object({{"gain", 2.5}, {"master", cv::object({{"name", "hi"}})}});
    const cv full = atp::studio::materialise(schema(), stored);

    EXPECT_DOUBLE_EQ(full.at("gain").as_double(), 2.5);
    EXPECT_EQ(full.at("master").at("name"), cv("hi"));
    EXPECT_EQ(full.at("master").at("upto"), cv(0)) << "and fills the rest of the group around it";
}

TEST(ConfigShape, MaterialiseFillsEveryElementOfAList) {
    const cv stored = cv::object({{"bands", cv::array({cv::object({{"upto", 10}})})}});
    const cv full = atp::studio::materialise(schema(), stored);

    ASSERT_EQ(full.at("bands").size(), 1u);
    EXPECT_EQ(full.at("bands")[0].at("upto"), cv(10));
    EXPECT_EQ(full.at("bands")[0].at("name"), cv("?")) << "an element is materialised like any other object";
}

TEST(ConfigShape, AKeyTheSchemaDoesNotDeclareSurvivesBothWays) {
    const cv stored = cv::object({{"legacy", true}, {"gain", 2.5}});
    const cv full = atp::studio::materialise(schema(), stored);
    EXPECT_EQ(full.at("legacy"), cv(true)) << "the tree shows it, which the form could not";

    const cv back = atp::studio::strip_defaults(schema(), full);
    EXPECT_EQ(back.at("legacy"), cv(true)) << "and it is written back untouched";
}

TEST(ConfigShape, StripDropsWhatEqualsItsDefaultAndKeepsTheRest) {
    const cv full = atp::studio::materialise(schema(), cv::object({{"gain", 2.5}}));
    const cv thin = atp::studio::strip_defaults(schema(), full);

    EXPECT_DOUBLE_EQ(thin.at("gain").as_double(), 2.5);
    EXPECT_EQ(thin.find("invert"), nullptr) << "opening a module must not grow its config to the full schema";
    EXPECT_EQ(thin.find("master"), nullptr) << "a group whose fields are all defaults is nothing to say";
    EXPECT_EQ(thin.find("bands"), nullptr);
}

TEST(ConfigShape, StripKeepsAnEmptyRequiredFieldOutSoTheModuleFailsHonestly) {
    const cv full = atp::studio::materialise(schema(), empty_object());
    const cv thin = atp::studio::strip_defaults(schema(), full);

    EXPECT_EQ(thin.find("device"), nullptr)
        << "writing an empty string would satisfy 'required and absent' and hand the module an empty value";
}

TEST(ConfigShape, AListKeepsItsLengthWhileAnElementMayThinToNothing) {
    const cv full = atp::studio::materialise(
        schema(), cv::object({{"bands", cv::array({empty_object(), cv::object({{"upto", 30}})})}}));
    const cv thin = atp::studio::strip_defaults(schema(), full);

    ASSERT_EQ(thin.at("bands").size(), 2u) << "dropping an element would renumber the ones after it";
    EXPECT_EQ(thin.at("bands")[0].size(), 0u) << "an element at its defaults says nothing, and that is not a hole";
    EXPECT_EQ(thin.at("bands")[1].at("upto"), cv(30));

    const cv again = atp::studio::materialise(schema(), thin);
    EXPECT_EQ(again.at("bands")[0].at("upto"), cv(0)) << "and the empty element comes back as its defaults";
    EXPECT_EQ(again.at("bands")[0].at("name"), cv("?"));
}

TEST(ConfigShape, TheTwoAreInverseOnAnythingTheDocumentMayHold) {
    const std::vector<cv> cases{
        empty_object(),
        cv::object({{"gain", 2.5}}),
        cv::object({{"legacy", 1}, {"invert", true}}),
        cv::object({{"tags", cv::array({"live"})}}),
        cv::object({{"bands", cv::array({cv::object({{"name", "small"}})})}}),
        cv::object({{"device", "hw:0"}, {"master", cv::object({{"upto", 7}})}}),
    };
    for (const cv& stored : cases) {
        const cv once = atp::studio::strip_defaults(schema(), stored);
        const cv twice = atp::studio::strip_defaults(schema(), atp::studio::materialise(schema(), stored));
        EXPECT_EQ(atp::runtime::json_dump(once), atp::runtime::json_dump(twice))
            << "materialise must add nothing strip does not take back: " << atp::runtime::json_dump(stored);
    }
}

TEST(ConfigShape, ARequiredFieldCanHoldTheZeroOfItsType) {
    const cv stored = cv::object({{"channel", 0}});
    const cv thin = atp::studio::strip_defaults(schema(), atp::studio::materialise(schema(), stored));

    ASSERT_NE(thin.find("channel"), nullptr)
        << "0 is a value somebody typed; dropping it makes the module fail on 'required and absent'";
    EXPECT_EQ(thin.at("channel"), cv(0));
}

TEST(ConfigShape, AnUntouchedRequiredFieldIsShownEmptyAndStaysAbsent) {
    const cv full = atp::studio::materialise(schema(), empty_object());

    ASSERT_NE(full.find("channel"), nullptr);
    EXPECT_TRUE(full.at("channel").is_null()) << "null is what an editor shows as an empty cell";
    EXPECT_EQ(atp::studio::strip_defaults(schema(), full).find("channel"), nullptr);
}

TEST(ConfigShape, AStoredValueOfTheWrongShapeIsLeftAloneRatherThanReplaced) {
    const cv stored = cv::object({{"bands", cv::object({{"at", 1}})}, {"master", "not an object"}});
    const cv full = atp::studio::materialise(schema(), stored);

    EXPECT_TRUE(full.at("bands").is_object()) << "the schema says list, the document says object — that is the "
                                                 "document's problem to fix, and validation says so";
    EXPECT_EQ(full.at("master"), cv("not an object"));

    const cv thin = atp::studio::strip_defaults(schema(), full);
    EXPECT_EQ(thin.at("bands"), stored.at("bands")) << "and an edit elsewhere must not delete it";
    EXPECT_EQ(thin.at("master"), cv("not an object"));
}
