// SPDX-License-Identifier: Apache-2.0
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <atp/config/node.hpp>
#include <atp/runtime/config_validator.hpp>
#include <atp/runtime/json_codec.hpp>

namespace {

using json = atp::config::node;

json valid_config() {
    return atp::runtime::json_parse(R"({
        "version": "3.0",
        "plugins": ["demo.dll"],
        "pipeline": {
            "modules": [
                {"group": "left", "modules": [{"module": "counter", "name": "ticks"}],
                 "expose": {"outputs": {"out": "ticks.count"}}},
                {"group": "right", "modules": [{"module": "printer"}],
                 "expose": {"inputs": {"in": "printer.value"}}}
            ],
            "connections": [{"from": "left.out", "to": "right.in"}]
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
    EXPECT_FALSE(check(doc).empty());

    doc = valid_config();
    doc["version"] = "4.0";
    EXPECT_FALSE(check(doc).empty());

    doc["version"] = "3.99";
    EXPECT_FALSE(check(doc).empty());

    doc["version"] = "not-a-version";
    EXPECT_FALSE(check(doc).empty());
}

TEST(ConfigValidator, AcceptsEveryMinorUpToTheSupportedOne) {
    json doc = valid_config();
    doc["version"] = "3.0";
    EXPECT_TRUE(check(doc).empty());

    doc["version"] = "3.1";
    EXPECT_TRUE(check(doc).empty());

    doc["version"] = "3.2";
    EXPECT_TRUE(check(doc).empty());

    doc["version"] = "3.3";
    EXPECT_TRUE(check(doc).empty());

    doc["version"] = "3.4";
    EXPECT_FALSE(check(doc).empty());
}

TEST(ConfigValidator, RejectsTheRemovedReplayKey) {
    const json doc = atp::runtime::json_parse(R"({
        "version": "3.0",
        "pipeline": {
            "modules": [{"module": "left"}, {"module": "right"}],
            "connections": [{"from": "left.out", "to": "right.in", "replay": true}]
        }
    })");
    const auto errors = check(doc);
    ASSERT_FALSE(errors.empty());
    EXPECT_NE(errors[0].find("replay"), std::string::npos);
}

TEST(ConfigValidator, RejectsTheSupersededSchemaVersion) {
    const json doc = atp::runtime::json_parse(R"({"version": "2.0", "pipeline": {"modules": []}})");
    const auto errors = check(doc);
    ASSERT_FALSE(errors.empty());
    EXPECT_NE(errors[0].find(atp::runtime::config_schema_version.to_string()), std::string::npos);
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
    doc["pipeline"]["modules"][0]["modules"][0] = json::object({{"module", "m"}, {"group", "g"}});
    EXPECT_FALSE(check(doc).empty());

    doc["pipeline"]["modules"][0]["modules"][0] = json::object({{"name", "orphan"}});
    EXPECT_FALSE(check(doc).empty());
}

TEST(ConfigValidator, DuplicateChildNamesInGroup) {
    json doc = valid_config();
    doc["pipeline"]["modules"][0]["modules"] =
        json::array({json::object({{"module", "counter"}}), json::object({{"module", "counter"}})});
    EXPECT_FALSE(check(doc).empty());
}

TEST(ConfigValidator, PathSyntax) {
    json doc = valid_config();
    doc["pipeline"]["connections"][0]["from"] = "no_dot";
    EXPECT_FALSE(check(doc).empty());

    doc = valid_config();
    doc["pipeline"]["modules"][0]["expose"]["outputs"]["out"] = "a.b.c";
    EXPECT_FALSE(check(doc).empty());
}

TEST(ConfigValidator, ThreadRules) {
    json doc = valid_config();
    doc["threads"][1].erase("period_ms");
    EXPECT_FALSE(check(doc).empty());

    doc = valid_config();
    doc["threads"][0]["period_ms"] = 5;
    EXPECT_FALSE(check(doc).empty());

    doc = valid_config();
    doc["threads"][1]["name"] = "a";
    EXPECT_FALSE(check(doc).empty());

    doc = valid_config();
    doc["threads"][0]["mode"] = "warp";
    EXPECT_FALSE(check(doc).empty());
}

TEST(ConfigValidator, AssignRules) {
    json doc = valid_config();
    doc["assign"]["left"] = "nowhere";
    EXPECT_FALSE(check(doc).empty());

    doc = valid_config();
    doc["assign"]["ghost"] = "a";
    EXPECT_FALSE(check(doc).empty());
}

TEST(ConfigValidator, PropertiesMustBeScalarObject) {
    const atp::config::node doc = atp::runtime::json_parse(R"({
        "version": "3.0",
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
    const atp::config::node doc = atp::runtime::json_parse(R"({
        "version": "3.0",
        "pipeline": {"modules": [{"module": "m", "params": {"x": 1}}]}
    })");
    const auto errors = atp::runtime::validate(doc);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors[0].find("unknown key 'params'"), std::string::npos);
}

TEST(ConfigValidator, OldChildrenKeyIsRejected) {
    const atp::config::node doc = atp::runtime::json_parse(R"({
        "version": "3.0",
        "pipeline": {"children": [{"module": "m"}]}
    })");
    const auto errors = atp::runtime::validate(doc);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors[0].find("unknown key 'children'"), std::string::npos);
}

TEST(ConfigValidator, AggregatesAllErrors) {
    json doc = valid_config();
    doc["version"] = "4.0";
    doc["threads"][0]["mode"] = "warp";
    doc["assign"]["left"] = "nowhere";
    EXPECT_GE(check(doc).size(), 3u);
}

TEST(ConfigValidator, ConfigMustBeObjectOrString) {
    const json doc = atp::runtime::json_parse(R"({
        "version": "3.2",
        "pipeline": {"modules": [{"module": "m", "config": [1, 2]}]}
    })");
    const std::vector<std::string> errors = check(doc);
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_NE(errors[0].find("must be an object or a string"), std::string::npos);
}

TEST(ConfigValidator, ConfigReferenceMustExist) {
    const json doc = atp::runtime::json_parse(R"({
        "version": "3.2",
        "configs": {"rig": {}},
        "pipeline": {"modules": [{"module": "m", "config": "absent"}]}
    })");
    const std::vector<std::string> errors = check(doc);
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_NE(errors[0].find("absent"), std::string::npos);
}

TEST(ConfigValidator, UnknownConfigPrefixIsNamed) {
    const json doc = atp::runtime::json_parse(R"({
        "version": "3.3",
        "pipeline": {"modules": [{"module": "m", "config": "literal:{}"}]}
    })");
    const std::vector<std::string> errors = check(doc);
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_NE(errors[0].find("literal"), std::string::npos);
}

TEST(ConfigValidator, AcceptsAFileConfigOnAModule) {
    const json doc = atp::runtime::json_parse(R"({
        "version": "3.3",
        "pipeline": {"modules": [{"module": "m", "config": "file:rig.yaml"}]}
    })");
    EXPECT_TRUE(check(doc).empty());
}

TEST(ConfigValidator, AcceptsAFileEntryInConfigs) {
    const json doc = atp::runtime::json_parse(R"({
        "version": "3.3",
        "configs": {"rig": "file:rig.json"},
        "pipeline": {"modules": [{"module": "m", "config": "rig"}]}
    })");
    EXPECT_TRUE(check(doc).empty());
}

TEST(ConfigValidator, RefusesAReferenceInsideConfigs) {
    const json doc = atp::runtime::json_parse(R"({
        "version": "3.3",
        "configs": {"rig": "other", "other": {}},
        "pipeline": {"modules": [{"module": "m", "config": "rig"}]}
    })");
    const std::vector<std::string> errors = check(doc);
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_NE(errors[0].find("configs.rig"), std::string::npos);
    EXPECT_NE(errors[0].find("must be an object or a 'file:' path"), std::string::npos);
}

TEST(ConfigValidator, RefusesAnEmptyFilePath) {
    const json doc = atp::runtime::json_parse(R"({
        "version": "3.3",
        "pipeline": {"modules": [{"module": "m", "config": "file:"}]}
    })");
    const std::vector<std::string> errors = check(doc);
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_NE(errors[0].find("names no file"), std::string::npos);
}

TEST(ConfigValidator, ConfigsKeyMayNotContainAColon) {
    const json doc = atp::runtime::json_parse(R"({
        "version": "3.2",
        "configs": {"a:b": {}},
        "pipeline": {"modules": []}
    })");
    const std::vector<std::string> errors = check(doc);
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_NE(errors[0].find("a:b"), std::string::npos);
}

TEST(ConfigValidator, ConfigsContentIsNotSchemaChecked) {
    const json doc = atp::runtime::json_parse(R"({
        "version": "3.2",
        "configs": {"rig": {"anything": [1, "two", {"three": null}]}},
        "pipeline": {"modules": [{"module": "m", "config": "rig"}]}
    })");
    EXPECT_TRUE(check(doc).empty());
}

TEST(ConfigValidator, ConfigsEntryMustBeAnObject) {
    const json doc = atp::runtime::json_parse(R"({
        "version": "3.2",
        "configs": {"rig": [1, 2]},
        "pipeline": {"modules": []}
    })");
    const std::vector<std::string> errors = check(doc);
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_NE(errors[0].find("rig"), std::string::npos);
}

TEST(ConfigValidator, BrokenConfigsBlockDoesNotAlsoBlameEveryReference) {
    const json doc = atp::runtime::json_parse(R"({
        "version": "3.2",
        "configs": [],
        "pipeline": {"modules": [{"module": "m", "config": "rig"}]}
    })");
    const std::vector<std::string> errors = check(doc);
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_NE(errors[0].find("configs"), std::string::npos);
}

TEST(ConfigValidator, SchemaThreeOneStillLoads) {
    const json doc = atp::runtime::json_parse(R"({
        "version": "3.1",
        "pipeline": {"modules": [{"module": "m"}]}
    })");
    EXPECT_TRUE(check(doc).empty());
}

}  // namespace
