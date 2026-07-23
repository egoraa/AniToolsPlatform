#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atp/runtime/config_validator.hpp>

namespace {

using nlohmann::json;

// Валидный скелет; тесты портят его точечно.
json valid_config() {
    return json::parse(R"({
        "version": "1.0",
        "plugins": ["demo.dll"],
        "pipeline": {
            "children": [
                {"group": "left", "children": [{"module": "counter", "name": "ticks"}],
                 "expose": {"outputs": {"out": "ticks.count"}}},
                {"group": "right", "children": [{"module": "printer"}],
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
    EXPECT_FALSE(check(doc).empty());  // обязательное поле

    doc = valid_config();
    doc["version"] = "2.0";  // чужой мажор
    EXPECT_FALSE(check(doc).empty());

    doc["version"] = "1.99";  // минор из будущего
    EXPECT_FALSE(check(doc).empty());

    doc["version"] = "not-a-version";
    EXPECT_FALSE(check(doc).empty());
}

TEST(ConfigValidator, UnknownKeysRejectedWithPath) {
    json doc = valid_config();
    doc["pipeline"]["children"][0]["typo"] = 1;
    const auto errors = check(doc);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors[0].find("pipeline.children[0]"), std::string::npos);
}

TEST(ConfigValidator, ChildMustBeModuleXorGroup) {
    json doc = valid_config();
    doc["pipeline"]["children"][0]["children"][0] = {{"module", "m"}, {"group", "g"}};
    EXPECT_FALSE(check(doc).empty());

    doc["pipeline"]["children"][0]["children"][0] = {{"name", "orphan"}};  // ни того, ни другого
    EXPECT_FALSE(check(doc).empty());
}

TEST(ConfigValidator, DuplicateChildNamesInGroup) {
    json doc = valid_config();
    // имя по умолчанию = имя фабрики: два безымянных counter конфликтуют
    doc["pipeline"]["children"][0]["children"] = {{{"module", "counter"}}, {{"module", "counter"}}};
    EXPECT_FALSE(check(doc).empty());
}

TEST(ConfigValidator, PathSyntax) {
    json doc = valid_config();
    doc["pipeline"]["connections"][0]["from"] = "no_dot";
    EXPECT_FALSE(check(doc).empty());

    doc = valid_config();
    doc["pipeline"]["children"][0]["expose"]["outputs"]["out"] = "a.b.c";  // ровно одна точка
    EXPECT_FALSE(check(doc).empty());
}

TEST(ConfigValidator, ThreadRules) {
    json doc = valid_config();
    doc["threads"][1].erase("period_ms");  // throttled без периода
    EXPECT_FALSE(check(doc).empty());

    doc = valid_config();
    doc["threads"][0]["period_ms"] = 5;  // период вне throttled
    EXPECT_FALSE(check(doc).empty());

    doc = valid_config();
    doc["threads"][1]["name"] = "a";  // дубликат имени
    EXPECT_FALSE(check(doc).empty());

    doc = valid_config();
    doc["threads"][0]["mode"] = "warp";  // неизвестный режим
    EXPECT_FALSE(check(doc).empty());
}

TEST(ConfigValidator, AssignRules) {
    json doc = valid_config();
    doc["assign"]["left"] = "nowhere";  // неизвестный поток
    EXPECT_FALSE(check(doc).empty());

    doc = valid_config();
    doc["assign"]["ghost"] = "a";  // путь не ведёт к группе
    EXPECT_FALSE(check(doc).empty());
}

TEST(ConfigValidator, AggregatesAllErrors) {
    json doc = valid_config();
    doc["version"] = "2.0";
    doc["threads"][0]["mode"] = "warp";
    doc["assign"]["left"] = "nowhere";
    EXPECT_GE(check(doc).size(), 3u);  // все три — за один проход
}

}  // namespace
