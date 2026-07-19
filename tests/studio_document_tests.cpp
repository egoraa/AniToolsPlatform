#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atp/app/config_validator.hpp>
#include <atp/studio/document.hpp>

namespace {

class DocumentFiles : public ::testing::Test {
   protected:
    void SetUp() override {
        dir_ = std::filesystem::path(::testing::TempDir()) /
               ::testing::UnitTest::GetInstance()->current_test_info()->name();
        std::filesystem::create_directories(dir_);
    }

    std::filesystem::path write(const std::string& name, const std::string& text) {
        const std::filesystem::path path = dir_ / name;
        std::ofstream(path) << text;
        return path;
    }

    std::filesystem::path dir_;
};

constexpr const char* sample_config = R"({
    "version": "1.0",
    "pipeline": {
        "children": [
            {"group": "left", "children": [{"module": "src"}],
             "expose": {"outputs": {"out": "src.value"}}},
            {"group": "right", "children": [{"module": "sink"}],
             "expose": {"inputs": {"in": "sink.value"}}}
        ],
        "connections": [{"from": "left.out", "to": "right.in"}]
    },
    "threads": [{"name": "t", "mode": "on_demand"}],
    "assign": {"left": "t"}
})";

TEST_F(DocumentFiles, CreateStartsEmptyWithCurrentSchema) {
    const atp::studio::document doc = atp::studio::document::create();
    EXPECT_EQ(doc.config().schema, atp::app::config_schema_version);
    EXPECT_TRUE(doc.config().pipeline.children.empty());
    EXPECT_FALSE(doc.had_includes());
}

TEST_F(DocumentFiles, OpenReadsModelAndGroupAtNavigates) {
    const auto file = write("doc.json", sample_config);
    const atp::studio::document doc = atp::studio::document::open(file);

    ASSERT_NE(doc.group_at(""), nullptr);  // корень
    ASSERT_NE(doc.group_at("left"), nullptr);
    EXPECT_EQ(doc.group_at("left")->name, "left");
    EXPECT_EQ(doc.group_at("left.src"), nullptr);  // модуль — не группа
    EXPECT_EQ(doc.group_at("ghost"), nullptr);
    EXPECT_FALSE(doc.had_includes());
}

TEST_F(DocumentFiles, OpenRejectsInvalidConfigWithAggregatedErrors) {
    const auto file = write("bad.json", R"({"version": "1.0", "pipeline": {"typo": 1}})");
    try {
        (void)atp::studio::document::open(file);
        FAIL() << "expected config_error";
    } catch (const atp::app::config_error& e) {
        EXPECT_NE(std::string(e.what()).find("typo"), std::string::npos);  // текст валидатора дошёл
    }
}

TEST_F(DocumentFiles, OpenFlagsIncludes) {
    write("part.json", R"({"group": "g", "children": []})");
    const auto file = write("doc.json", R"({
        "version": "1.0",
        "pipeline": {"children": [{"$include": "part.json"}]}
    })");
    const atp::studio::document doc = atp::studio::document::open(file);
    EXPECT_TRUE(doc.had_includes());        // сохранение расплющит — предупредить
    ASSERT_NE(doc.group_at("g"), nullptr);  // документ открыт развёрнутым
}

TEST_F(DocumentFiles, SaveWritesValidConfigAndLayoutSidecar) {
    const auto file = write("doc.json", sample_config);
    atp::studio::document doc = atp::studio::document::open(file);
    doc.set_position("left", {10.0f, 20.0f});
    doc.set_position("left.src", {30.0f, 40.0f});

    const auto saved = dir_ / "out.json";
    doc.save(saved);

    std::ifstream in(saved);
    const nlohmann::json written = nlohmann::json::parse(in);
    EXPECT_TRUE(atp::app::validate(written).empty());  // файл пригоден для atp_app

    const atp::studio::document reopened = atp::studio::document::open(saved);
    ASSERT_TRUE(reopened.position("left.src").has_value());
    EXPECT_EQ(*reopened.position("left.src"), (atp::studio::node_position{30.0f, 40.0f}));
    EXPECT_EQ(reopened.position("ghost"), std::nullopt);
}

}  // namespace
