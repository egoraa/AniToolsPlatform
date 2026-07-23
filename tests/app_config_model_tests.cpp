#include <chrono>
#include <string>
#include <utility>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atp/runtime/config_model.hpp>
#include <atp/runtime/config_validator.hpp>

namespace {

TEST(ConfigDecode, BuildsTypedModelPreservingOrder) {
    const nlohmann::json doc = nlohmann::json::parse(R"({
        "version": "1.0",
        "plugins": ["p.dll"],
        "pipeline": {
            "children": [
                {"module": "counter", "name": "ticks", "version": "1.0", "params": {"rate": 10}},
                {"group": "sub", "children": [{"module": "printer"}],
                 "expose": {"inputs": {"in": "printer.value"}}}
            ],
            "connections": [{"from": "ticks.count", "to": "sub.in", "replay": true}]
        },
        "threads": [{"name": "t", "mode": "throttled", "period_ms": 5}],
        "assign": {"sub": "t"}
    })");
    ASSERT_TRUE(atp::runtime::validate(doc).empty());

    const atp::runtime::config cfg = atp::runtime::decode(doc);
    EXPECT_EQ(cfg.schema, (atp::version{1, 0}));
    ASSERT_EQ(cfg.plugins.size(), 1u);

    ASSERT_EQ(cfg.pipeline.children.size(), 2u);
    ASSERT_TRUE(cfg.pipeline.children[0].module.has_value());  // порядок массива сохранён
    EXPECT_EQ(cfg.pipeline.children[0].module->factory, "counter");
    EXPECT_EQ(cfg.pipeline.children[0].module->name, "ticks");
    EXPECT_EQ(cfg.pipeline.children[0].module->factory_version, (atp::version{1, 0}));
    EXPECT_EQ(cfg.pipeline.children[0].module->params, R"({"rate":10})");  // компактный dump

    ASSERT_TRUE(cfg.pipeline.children[1].group != nullptr);
    EXPECT_EQ(cfg.pipeline.children[1].group->name, "sub");
    ASSERT_EQ(cfg.pipeline.children[1].group->expose_inputs.size(), 1u);
    EXPECT_EQ(cfg.pipeline.children[1].group->expose_inputs[0].second, "printer.value");

    ASSERT_EQ(cfg.pipeline.connections.size(), 1u);
    EXPECT_TRUE(cfg.pipeline.connections[0].replay);

    ASSERT_EQ(cfg.threads.size(), 1u);
    EXPECT_EQ(cfg.threads[0].mode, atp::thread_mode::throttled);
    EXPECT_EQ(cfg.threads[0].period, std::chrono::milliseconds(5));

    ASSERT_EQ(cfg.assignments.size(), 1u);
    EXPECT_EQ(cfg.assignments[0], (std::pair<std::string, std::string>{"sub", "t"}));
}

TEST(ConfigDecode, DefaultsNameToFactoryAndOmittedFields) {
    const nlohmann::json doc = nlohmann::json::parse(R"({
        "version": "1.0",
        "pipeline": {"children": [{"module": "counter"}]}
    })");
    ASSERT_TRUE(atp::runtime::validate(doc).empty());

    const atp::runtime::config cfg = atp::runtime::decode(doc);
    EXPECT_TRUE(cfg.plugins.empty());
    EXPECT_TRUE(cfg.threads.empty());
    ASSERT_EQ(cfg.pipeline.children.size(), 1u);
    EXPECT_EQ(cfg.pipeline.children[0].module->name, "counter");  // дефолт — имя фабрики
    EXPECT_EQ(cfg.pipeline.children[0].module->factory_version, std::nullopt);
    EXPECT_TRUE(cfg.pipeline.children[0].module->params.empty());
}

TEST(ConfigEncode, RoundTripsCanonicalDocument) {
    // Канон: дефолты опущены, mode указан явно, алиасы expose в алфавитном
    // порядке (JSON-объект их сортирует) — encode выдаёт ровно этот документ.
    const nlohmann::json doc = nlohmann::json::parse(R"({
        "version": "1.0",
        "plugins": ["p.dll"],
        "pipeline": {
            "children": [
                {"module": "counter", "name": "ticks", "version": "1.0", "params": {"rate": 10}},
                {"group": "sub", "children": [{"module": "printer"}],
                 "expose": {"inputs": {"in": "printer.value"}}}
            ],
            "connections": [{"from": "ticks.count", "to": "sub.in", "replay": true}]
        },
        "threads": [{"name": "t", "mode": "throttled", "period_ms": 5}],
        "assign": {"sub": "t"}
    })");
    ASSERT_TRUE(atp::runtime::validate(doc).empty());
    EXPECT_EQ(atp::runtime::encode(atp::runtime::decode(doc)), doc);
}

TEST(ConfigEncode, OmitsDefaultsAndStaysValid) {
    const nlohmann::json doc = nlohmann::json::parse(R"({
        "version": "1.0",
        "pipeline": {
            "children": [
                {"module": "counter", "name": "counter"},
                {"group": "g", "children": [], "connections": []}
            ]
        },
        "threads": [{"name": "t"}]
    })");
    ASSERT_TRUE(atp::runtime::validate(doc).empty());

    const nlohmann::json out = atp::runtime::encode(atp::runtime::decode(doc));
    // канонизация: имя == фабрике опущено, пустые разделы исчезли, mode явный
    EXPECT_FALSE(out.at("pipeline").at("children").at(0).contains("name"));
    EXPECT_FALSE(out.at("pipeline").at("children").at(1).contains("children"));
    EXPECT_FALSE(out.at("pipeline").at("children").at(1).contains("connections"));
    EXPECT_EQ(out.at("threads").at(0).at("mode"), "on_demand");
    EXPECT_TRUE(atp::runtime::validate(out).empty());  // encode выдаёт валидный конфиг
}

}  // namespace
