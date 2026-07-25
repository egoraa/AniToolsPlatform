#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atp/runtime/config_validator.hpp>
#include <atp/studio/document.hpp>

namespace {

class DocumentFiles : public ::testing::Test {
   protected:
    void SetUp() override {
        dir_ = std::filesystem::temp_directory_path() / ::testing::UnitTest::GetInstance()->current_test_info()->name();
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
    EXPECT_EQ(doc.config().schema, atp::runtime::config_schema_version);
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
    } catch (const atp::runtime::config_error& e) {
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
    EXPECT_TRUE(atp::runtime::validate(written).empty());  // файл пригоден для atp_app

    const atp::studio::document reopened = atp::studio::document::open(saved);
    ASSERT_TRUE(reopened.position("left.src").has_value());
    EXPECT_EQ(*reopened.position("left.src"), (atp::studio::node_position{30.0f, 40.0f}));
    EXPECT_EQ(reopened.position("ghost"), std::nullopt);
}

TEST(DocumentEdit, BuildsPipelineThroughOperations) {
    atp::studio::document doc = atp::studio::document::create();
    doc.add_group("", "left");
    doc.add_module("left", "counter", "ticks", atp::version{1, 0});
    doc.set_expose_output("left", "out", "ticks.count");
    doc.add_group("", "right");
    doc.add_module("right", "printer");  // имя по умолчанию — имя фабрики
    doc.set_expose_input("right", "in", "printer.value");
    doc.connect("", "left.out", "right.in", true);
    doc.add_thread("main_loop", atp::thread_mode::throttled, std::chrono::milliseconds(5));
    doc.set_assignment("left", "main_loop");
    doc.add_plugin("libdemo.dll");
    doc.add_plugin("libdemo.dll");  // дедуп: не операция, undo не растёт

    const atp::runtime::config& cfg = doc.config();
    ASSERT_EQ(cfg.pipeline.children.size(), 2u);
    EXPECT_EQ(cfg.pipeline.children[1].group->children[0].module->name, "printer");
    ASSERT_EQ(cfg.pipeline.connections.size(), 1u);
    EXPECT_TRUE(cfg.pipeline.connections[0].replay);
    ASSERT_EQ(cfg.threads.size(), 1u);
    ASSERT_EQ(cfg.assignments.size(), 1u);
    ASSERT_EQ(cfg.plugins.size(), 1u);
    // построенное операциями проходит полный validate — инварианты те же
    EXPECT_TRUE(atp::runtime::validate(atp::runtime::encode(cfg)).empty());
}

TEST(DocumentEdit, RejectsInvariantViolationsWithoutPollutingUndo) {
    atp::studio::document doc = atp::studio::document::create();
    doc.add_group("", "g");
    doc.add_module("g", "counter");

    EXPECT_THROW(doc.add_module("g", "counter"), atp::runtime::config_error);      // дубликат имени
    EXPECT_THROW(doc.add_module("ghost", "counter"), atp::runtime::config_error);  // нет группы
    EXPECT_THROW(doc.add_group("g", "bad.name"), atp::runtime::config_error);      // точка в имени
    EXPECT_THROW(doc.connect("g", "no_dot", "counter.in"), atp::runtime::config_error);
    EXPECT_THROW(doc.connect("g", "ghost.out", "counter.in"), atp::runtime::config_error);       // нет ребёнка
    EXPECT_THROW(doc.add_thread("t", atp::thread_mode::throttled), atp::runtime::config_error);  // период
    EXPECT_THROW(doc.set_assignment("g", "nowhere"), atp::runtime::config_error);                // нет потока

    // после отказов откатываются ровно две успешные операции — и всё
    EXPECT_TRUE(doc.undo());
    EXPECT_TRUE(doc.undo());
    EXPECT_FALSE(doc.can_undo());
    EXPECT_TRUE(doc.config().pipeline.children.empty());
}

TEST(DocumentEdit, RemoveChildCleansReferences) {
    atp::studio::document doc = atp::studio::document::create();
    doc.add_group("", "g");
    doc.add_module("g", "a");
    doc.add_module("g", "b");
    doc.connect("g", "a.out", "b.in");
    doc.set_expose_output("g", "alias", "a.out");
    doc.add_thread("t", atp::thread_mode::on_demand);
    doc.set_assignment("g", "t");
    doc.set_position("g.a", {1.0f, 2.0f});

    doc.remove_child("g", "a");
    EXPECT_TRUE(doc.group_at("g")->connections.empty());     // связь с "a." умерла
    EXPECT_TRUE(doc.group_at("g")->expose_outputs.empty());  // и экспорт
    EXPECT_EQ(doc.position("g.a"), std::nullopt);

    doc.remove_child("", "g");                      // подгруппа целиком
    EXPECT_TRUE(doc.config().assignments.empty());  // раскладка не осиротела
}

TEST(DocumentEdit, RenameChildRewritesReferences) {
    atp::studio::document doc = atp::studio::document::create();
    doc.add_group("", "g");
    doc.add_module("g", "a");
    doc.add_module("g", "b");
    doc.connect("g", "a.out", "b.in");
    doc.set_expose_output("g", "alias", "a.out");
    doc.set_position("g.a", {1.0f, 2.0f});

    doc.rename_child("g", "a", "producer");
    EXPECT_EQ(doc.group_at("g")->connections[0].from, "producer.out");
    EXPECT_EQ(doc.group_at("g")->expose_outputs[0].second, "producer.out");
    EXPECT_EQ(doc.position("g.producer"), (atp::studio::node_position{1.0f, 2.0f}));
    EXPECT_EQ(doc.position("g.a"), std::nullopt);

    doc.add_thread("t", atp::thread_mode::on_demand);
    doc.set_assignment("g", "t");
    doc.rename_child("", "g", "stage");  // и путь в раскладке
    EXPECT_EQ(doc.config().assignments[0].first, "stage");
}

TEST(DocumentEdit, UndoRedoWalkHistory) {
    atp::studio::document doc = atp::studio::document::create();
    EXPECT_FALSE(doc.can_undo());
    doc.add_group("", "g");
    doc.add_module("g", "m");
    EXPECT_TRUE(doc.undo());  // откатили модуль
    EXPECT_TRUE(doc.group_at("g")->children.empty());
    EXPECT_TRUE(doc.redo());  // вернули
    EXPECT_EQ(doc.group_at("g")->children.size(), 1u);
    EXPECT_TRUE(doc.undo());
    doc.add_module("g", "other");  // новая ветка истории
    EXPECT_FALSE(doc.can_redo());  // redo сброшен
}

TEST(DocumentEdit, SetPropertyAddsAndReplaces) {
    auto doc = atp::studio::document::create();
    doc.add_module("", "counter");
    doc.set_property("", "counter", "limit", 5);
    ASSERT_EQ(doc.config().pipeline.children[0].module->properties.size(), 1u);
    EXPECT_EQ(doc.config().pipeline.children[0].module->properties[0].second, nlohmann::json(5));
    doc.set_property("", "counter", "limit", 7);  // замена, не дубль
    ASSERT_EQ(doc.config().pipeline.children[0].module->properties.size(), 1u);
    EXPECT_EQ(doc.config().pipeline.children[0].module->properties[0].second, nlohmann::json(7));
    EXPECT_TRUE(doc.can_undo());
}

TEST(DocumentEdit, SetPropertyRejectsNonScalarAndGhostModule) {
    auto doc = atp::studio::document::create();
    doc.add_module("", "counter");
    EXPECT_THROW(doc.set_property("", "counter", "limit", nlohmann::json::object()), atp::runtime::config_error);
    EXPECT_THROW(doc.set_property("", "ghost", "limit", 5), atp::runtime::config_error);
}

TEST(DocumentEdit, ClearPropertyRemovesPair) {
    auto doc = atp::studio::document::create();
    doc.add_module("", "counter");
    doc.set_property("", "counter", "limit", 5);
    doc.clear_property("", "counter", "limit");
    EXPECT_TRUE(doc.config().pipeline.children[0].module->properties.empty());
    doc.clear_property("", "counter", "ghost");  // отсутствие пары — no-op, не ошибка
}

}  // namespace
