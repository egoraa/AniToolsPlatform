// SPDX-License-Identifier: Apache-2.0
#include <chrono>
#include <string>
#include <utility>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atp/runtime/config_model.hpp>
#include <atp/runtime/config_validator.hpp>

#include "support/required.hpp"

namespace {

TEST(ConfigDecode, BuildsTypedModelPreservingOrder) {
    const nlohmann::json doc = nlohmann::json::parse(R"({
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
    EXPECT_EQ(atp_tests::required(cfg.pipeline.modules[0].module).properties[0].second, nlohmann::json("a.txt"));
    EXPECT_EQ(atp_tests::required(cfg.pipeline.modules[0].module).properties[1].second, nlohmann::json(10));
    EXPECT_EQ(atp_tests::required(cfg.pipeline.modules[0].module).properties[2].second, nlohmann::json(true));

    ASSERT_TRUE(cfg.pipeline.modules[1].group != nullptr);
    EXPECT_EQ(cfg.pipeline.modules[1].group->name, "sub");
    ASSERT_EQ(cfg.pipeline.modules[1].group->expose_inputs.size(), 1u);
    EXPECT_EQ(cfg.pipeline.modules[1].group->expose_inputs[0].second, "printer.value");

    ASSERT_EQ(cfg.pipeline.connections.size(), 1u);
    EXPECT_EQ(cfg.pipeline.connections[0].from, "ticks.count");
    EXPECT_EQ(cfg.pipeline.connections[0].to, "sub.in");

    ASSERT_EQ(cfg.threads.size(), 1u);
    EXPECT_EQ(cfg.threads[0].mode, atp::thread_mode::throttled);
    EXPECT_EQ(cfg.threads[0].period, std::chrono::milliseconds(5));

    ASSERT_EQ(cfg.assignments.size(), 1u);
    EXPECT_EQ(cfg.assignments[0], (std::pair<std::string, std::string>{"sub", "t"}));
}

TEST(ConfigDecode, DefaultsNameToFactoryAndOmittedFields) {
    const nlohmann::json doc = nlohmann::json::parse(R"({
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
    const nlohmann::json doc = nlohmann::json::parse(R"({
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
    EXPECT_EQ(atp::runtime::encode(atp::runtime::decode(doc)), doc);
}

TEST(ConfigEncode, OmitsDefaultsAndStaysValid) {
    const nlohmann::json doc = nlohmann::json::parse(R"({
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

    const nlohmann::json out = atp::runtime::encode(atp::runtime::decode(doc));
    EXPECT_FALSE(out.at("pipeline").at("modules").at(0).contains("name"));
    EXPECT_FALSE(out.at("pipeline").at("modules").at(1).contains("modules"));
    EXPECT_FALSE(out.at("pipeline").at("modules").at(1).contains("connections"));
    EXPECT_EQ(out.at("threads").at(0).at("mode"), "on_demand");
    EXPECT_TRUE(atp::runtime::validate(out).empty());
}

TEST(ConfigModel, InlineConfigSurvivesRoundTrip) {
    const nlohmann::json doc = nlohmann::json::parse(R"({
        "version": "3.2",
        "pipeline": {"modules": [{"module": "resampler", "config": {"channels": [1, 2]}}]}
    })");
    EXPECT_EQ(atp::runtime::encode(atp::runtime::decode(doc)), doc);
}

TEST(ConfigModel, ConfigReferenceSurvivesRoundTripUnexpanded) {
    const nlohmann::json doc = nlohmann::json::parse(R"({
        "version": "3.2",
        "configs": {"rig": {"channels": [1, 2]}},
        "pipeline": {"modules": [{"module": "resampler", "config": "rig"}]}
    })");
    const nlohmann::json back = atp::runtime::encode(atp::runtime::decode(doc));
    EXPECT_EQ(back, doc);
    EXPECT_TRUE(back["pipeline"]["modules"][0]["config"].is_string());
}

TEST(ConfigModel, AbsentConfigIsNotWrittenBack) {
    const nlohmann::json doc = nlohmann::json::parse(R"({
        "version": "3.2",
        "pipeline": {"modules": [{"module": "resampler"}]}
    })");
    const nlohmann::json back = atp::runtime::encode(atp::runtime::decode(doc));
    EXPECT_FALSE(back["pipeline"]["modules"][0].contains("config"));
}

}  // namespace
