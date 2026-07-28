#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atp/runtime/config_validator.hpp>

namespace {

using nlohmann::json;

// A valid skeleton; the tests spoil it one field at a time.
json valid_config() {
    return json::parse(R"({
        "version": "2.0",
        "plugins": ["demo.dll"],
        "pipeline": {
            "modules": [
                {"group": "left", "modules": [{"module": "counter", "name": "ticks"}],
                 "expose": {"outputs": {"out": "ticks.count"}}},
                {"group": "right", "modules": [{"module": "printer"}],
                 "expose": {"inputs": {"in": "printer.value"}}}
            ],
            "connections": [{"from": "left.out", "to": "right.in", "replay": true}]
        },
        "threads": [
            {"name": "a", "mode": "on_demand"},
            {"name": "b", "mode": "throttled", "period_ms": 5}
        ],
        "assign": {"left": "a", "right": "b"}
    })");
}

std::vector<std::string> check(const json& doc) {
    return atp::runtime::validate(doc);
}

TEST(ConfigValidator, AcceptsValidConfig) {
    EXPECT_TRUE(check(valid_config()).empty());
}

TEST(ConfigValidator, VersionRules) {
    json doc = valid_config();
    doc.erase("version");
    EXPECT_FALSE(check(doc).empty());  // a required field

    doc = valid_config();
    doc["version"] = "3.0";  // a foreign major
    EXPECT_FALSE(check(doc).empty());

    doc["version"] = "2.99";  // a minor from the future
    EXPECT_FALSE(check(doc).empty());

    doc["version"] = "not-a-version";
    EXPECT_FALSE(check(doc).empty());
}

TEST(ConfigValidator, UnknownKeysRejectedWithPath) {
    json doc = valid_config();
    doc["pipeline"]["modules"][0]["typo"] = 1;
    const auto errors = check(doc);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors[0].find("pipeline.modules[0]"), std::string::npos);
}

TEST(ConfigValidator, ChildMustBeModuleXorGroup) {
    json doc = valid_config();
    doc["pipeline"]["modules"][0]["modules"][0] = {{"module", "m"}, {"group", "g"}};
    EXPECT_FALSE(check(doc).empty());

    doc["pipeline"]["modules"][0]["modules"][0] = {{"name", "orphan"}};  // neither of the two
    EXPECT_FALSE(check(doc).empty());
}

TEST(ConfigValidator, DuplicateChildNamesInGroup) {
    json doc = valid_config();
    // the default name is the factory name, so two unnamed counters collide
    doc["pipeline"]["modules"][0]["modules"] = {{{"module", "counter"}}, {{"module", "counter"}}};
    EXPECT_FALSE(check(doc).empty());
}

TEST(ConfigValidator, PathSyntax) {
    json doc = valid_config();
    doc["pipeline"]["connections"][0]["from"] = "no_dot";
    EXPECT_FALSE(check(doc).empty());

    doc = valid_config();
    doc["pipeline"]["modules"][0]["expose"]["outputs"]["out"] = "a.b.c";  // exactly one dot is allowed
    EXPECT_FALSE(check(doc).empty());
}

TEST(ConfigValidator, ThreadRules) {
    json doc = valid_config();
    doc["threads"][1].erase("period_ms");  // throttled without a period
    EXPECT_FALSE(check(doc).empty());

    doc = valid_config();
    doc["threads"][0]["period_ms"] = 5;  // a period outside throttled mode
    EXPECT_FALSE(check(doc).empty());

    doc = valid_config();
    doc["threads"][1]["name"] = "a";  // duplicate name
    EXPECT_FALSE(check(doc).empty());

    doc = valid_config();
    doc["threads"][0]["mode"] = "warp";  // unknown mode
    EXPECT_FALSE(check(doc).empty());
}

TEST(ConfigValidator, AssignRules) {
    json doc = valid_config();
    doc["assign"]["left"] = "nowhere";  // unknown thread
    EXPECT_FALSE(check(doc).empty());

    doc = valid_config();
    doc["assign"]["ghost"] = "a";  // the path leads to no group
    EXPECT_FALSE(check(doc).empty());
}

TEST(ConfigValidator, PropertiesMustBeScalarObject) {
    const nlohmann::json doc = nlohmann::json::parse(R"({
        "version": "2.0",
        "pipeline": {"modules": [
            {"module": "m1", "properties": {"ok": 5, "bad": {"nested": 1}}},
            {"module": "m2", "properties": [1, 2]}
        ]}
    })");
    const auto errors = atp::runtime::validate(doc);
    ASSERT_EQ(errors.size(), 2u);
    EXPECT_NE(errors[0].find("properties.bad"), std::string::npos);
    EXPECT_NE(errors[1].find("must be an object"), std::string::npos);
}

TEST(ConfigValidator, OldParamsKeyIsRejected) {
    const nlohmann::json doc = nlohmann::json::parse(R"({
        "version": "2.0",
        "pipeline": {"modules": [{"module": "m", "params": {"x": 1}}]}
    })");
    const auto errors = atp::runtime::validate(doc);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors[0].find("unknown key 'params'"), std::string::npos);
}

TEST(ConfigValidator, OldChildrenKeyIsRejected) {
    const nlohmann::json doc = nlohmann::json::parse(R"({
        "version": "2.0",
        "pipeline": {"children": [{"module": "m"}]}
    })");
    const auto errors = atp::runtime::validate(doc);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors[0].find("unknown key 'children'"), std::string::npos);
}

TEST(ConfigValidator, AggregatesAllErrors) {
    json doc = valid_config();
    doc["version"] = "3.0";
    doc["threads"][0]["mode"] = "warp";
    doc["assign"]["left"] = "nowhere";
    EXPECT_GE(check(doc).size(), 3u);  // all three in a single pass
}

}  // namespace
