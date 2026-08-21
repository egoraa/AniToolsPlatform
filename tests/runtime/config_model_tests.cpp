// SPDX-License-Identifier: Apache-2.0
#include <chrono>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include <atp/config/node.hpp>
#include <atp/runtime/config_model.hpp>
#include <atp/runtime/config_validator.hpp>
#include <atp/runtime/json_codec.hpp>

#include "support/required.hpp"

namespace {

TEST(ConfigDecode, BuildsTypedModelPreservingOrder) {
    const atp::config::node doc = atp::runtime::json_parse(R"({
        "version": "3.0",
        "plugins": ["p.dll"],
        "pipeline": {
            "modules": [
                {"module": "counter", "name": "ticks", "version": "1.0",
                 "properties": {"rate": 10, "file": "a.txt", "verbose": true}},
                {"group": "sub", "modules": [{"module": "printer"}],
                 "expose": {"inputs": {"in": "printer.value"}}}
            ],
            "connections": [{"from": "ticks.count", "to": "sub.in"}]
        },
        "threads": [{"name": "t", "mode": "throttled", "period_ms": 5}],
        "assign": {"sub": "t"}
    })");
    ASSERT_TRUE(atp::runtime::validate(doc).empty());

    const atp::runtime::config cfg = atp::runtime::decode(doc);
    EXPECT_EQ(cfg.schema, (atp::version{3, 0}));
    ASSERT_EQ(cfg.plugins.size(), 1u);

    ASSERT_EQ(cfg.pipeline.modules.size(), 2u);
    ASSERT_TRUE(cfg.pipeline.modules[0].module.has_value());
    EXPECT_EQ(atp_tests::required(cfg.pipeline.modules[0].module).factory, "counter");
    EXPECT_EQ(atp_tests::required(cfg.pipeline.modules[0].module).name, "ticks");
    EXPECT_EQ(atp_tests::required(cfg.pipeline.modules[0].module).factory_version, (atp::version{1, 0}));
    ASSERT_EQ(atp_tests::required(cfg.pipeline.modules[0].module).properties.size(), 3u);
    EXPECT_EQ(atp_tests::required(cfg.pipeline.modules[0].module).properties[0].first, "file");
    EXPECT_EQ(atp_tests::required(cfg.pipeline.modules[0].module).properties[0].second, atp::config::node("a.txt"));
    EXPECT_EQ(atp_tests::required(cfg.pipeline.modules[0].module).properties[1].second, atp::config::node(10));
    EXPECT_EQ(atp_tests::required(cfg.pipeline.modules[0].module).properties[2].second, atp::config::node(true));

    ASSERT_TRUE(cfg.pipeline.modules[1].group != nullptr);
    EXPECT_EQ(cfg.pipeline.modules[1].group->name, "sub");
    ASSERT_EQ(cfg.pipeline.modules[1].group->expose_inputs.size(), 1u);
    EXPECT_EQ(cfg.pipeline.modules[1].group->expose_inputs[0].second, "printer.value");

    ASSERT_EQ(cfg.pipeline.connections.size(), 1u);
    EXPECT_EQ(cfg.pipeline.connections[0].from, "ticks.count");
    EXPECT_EQ(cfg.pipeline.connections[0].to, "sub.in");

    ASSERT_EQ(cfg.threads.size(), 1u);
    EXPECT_EQ(cfg.threads[0].mode, atp::runtime::thread_mode::throttled);
    EXPECT_EQ(cfg.threads[0].period, std::chrono::milliseconds(5));

    ASSERT_EQ(cfg.assignments.size(), 1u);
    EXPECT_EQ(cfg.assignments[0], (std::pair<std::string, std::string>{"sub", "t"}));
}

TEST(ConfigDecode, DefaultsNameToFactoryAndOmittedFields) {
    const atp::config::node doc = atp::runtime::json_parse(R"({
        "version": "3.0",
        "pipeline": {"modules": [{"module": "counter"}]}
    })");
    ASSERT_TRUE(atp::runtime::validate(doc).empty());

    const atp::runtime::config cfg = atp::runtime::decode(doc);
    EXPECT_TRUE(cfg.plugins.empty());
    EXPECT_TRUE(cfg.threads.empty());
    ASSERT_EQ(cfg.pipeline.modules.size(), 1u);
    EXPECT_EQ(atp_tests::required(cfg.pipeline.modules[0].module).name, "counter");
    EXPECT_EQ(atp_tests::required(cfg.pipeline.modules[0].module).factory_version, std::nullopt);
    EXPECT_TRUE(atp_tests::required(cfg.pipeline.modules[0].module).properties.empty());
}

TEST(ConfigEncode, RoundTripsCanonicalDocument) {
    const atp::config::node doc = atp::runtime::json_parse(R"({
        "version": "3.0",
        "plugins": ["p.dll"],
        "pipeline": {
            "modules": [
                {"module": "counter", "name": "ticks", "version": "1.0",
                 "properties": {"rate": 10, "file": "a.txt", "verbose": true}},
                {"group": "sub", "modules": [{"module": "printer"}],
                 "expose": {"inputs": {"in": "printer.value"}}}
            ],
            "connections": [{"from": "ticks.count", "to": "sub.in"}]
        },
        "threads": [{"name": "t", "mode": "throttled", "period_ms": 5}],
        "assign": {"sub": "t"}
    })");
    ASSERT_TRUE(atp::runtime::validate(doc).empty());
    EXPECT_EQ(atp::runtime::json_dump(atp::runtime::encode(atp::runtime::decode(doc))), atp::runtime::json_dump(doc));
}

TEST(ConfigEncode, RoundTripIsPinnedOnTheTextNotTheTree) {
    const atp::config::node doc = atp::runtime::json_parse(R"({"version":"3.3","pipeline":{}})");
    const atp::config::node back = atp::runtime::encode(atp::runtime::decode(doc));
    EXPECT_EQ(atp::runtime::json_dump(back), atp::runtime::json_dump(doc));
    EXPECT_EQ(back.key_at(0), "version");
}

TEST(ConfigEncode, OmitsDefaultsAndStaysValid) {
    const atp::config::node doc = atp::runtime::json_parse(R"({
        "version": "3.0",
        "pipeline": {
            "modules": [
                {"module": "counter", "name": "counter"},
                {"group": "g", "modules": [], "connections": []}
            ]
        },
        "threads": [{"name": "t"}]
    })");
    ASSERT_TRUE(atp::runtime::validate(doc).empty());

    const atp::config::node out = atp::runtime::encode(atp::runtime::decode(doc));
    EXPECT_EQ(out.at("pipeline").at("modules")[0].find("name"), nullptr);
    EXPECT_EQ(out.at("pipeline").at("modules")[1].find("modules"), nullptr);
    EXPECT_EQ(out.at("pipeline").at("modules")[1].find("connections"), nullptr);
    EXPECT_EQ(out.at("threads")[0].string_at("mode"), "on_demand");
    EXPECT_TRUE(atp::runtime::validate(out).empty());
}

TEST(ConfigModel, InlineConfigSurvivesRoundTrip) {
    const atp::config::node doc = atp::runtime::json_parse(R"({
        "version": "3.2",
        "pipeline": {"modules": [{"module": "resampler", "config": {"channels": [1, 2]}}]}
    })");
    EXPECT_EQ(atp::runtime::json_dump(atp::runtime::encode(atp::runtime::decode(doc))), atp::runtime::json_dump(doc));
}

TEST(ConfigModel, ConfigReferenceSurvivesRoundTripUnexpanded) {
    const atp::config::node doc = atp::runtime::json_parse(R"({
        "version": "3.2",
        "configs": {"rig": {"channels": [1, 2]}},
        "pipeline": {"modules": [{"module": "resampler", "config": "rig"}]}
    })");
    const atp::config::node back = atp::runtime::encode(atp::runtime::decode(doc));
    EXPECT_EQ(atp::runtime::json_dump(back), atp::runtime::json_dump(doc));
    EXPECT_TRUE(back.at("pipeline").at("modules")[0].at("config").is_string());
}

TEST(ConfigModel, SchemaIsThreeThree) {
    EXPECT_EQ(atp::runtime::config_schema_version.parts[0], 3U);
    EXPECT_EQ(atp::runtime::config_schema_version.parts[1], 3U);
}

TEST(ConfigModel, AFileConfigSurvivesARoundTripUnexpanded) {
    const atp::config::node doc = atp::runtime::json_parse(R"({
        "version": "3.3",
        "configs": {"shared": "file:shared.json"},
        "pipeline": {"modules": [{"module": "resampler", "config": "file:rig.yaml"}]}
    })");
    const atp::config::node back = atp::runtime::encode(atp::runtime::decode(doc));
    EXPECT_EQ(atp::runtime::json_dump(back), atp::runtime::json_dump(doc));
    EXPECT_EQ(back.at("pipeline").at("modules")[0].string_at("config"), "file:rig.yaml");
    EXPECT_EQ(back.at("configs").string_at("shared"), "file:shared.json");
}

TEST(ConfigModel, AbsentConfigIsNotWrittenBack) {
    const atp::config::node doc = atp::runtime::json_parse(R"({
        "version": "3.2",
        "pipeline": {"modules": [{"module": "resampler"}]}
    })");
    const atp::config::node back = atp::runtime::encode(atp::runtime::decode(doc));
    EXPECT_EQ(back.at("pipeline").at("modules")[0].find("config"), nullptr);
}

}  // namespace
