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
    "version": "2.0",
    "pipeline": {
        "modules": [
            {"group": "left", "modules": [{"module": "src"}],
             "expose": {"outputs": {"out": "src.value"}}},
            {"group": "right", "modules": [{"module": "sink"}],
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
    EXPECT_TRUE(doc.config().pipeline.modules.empty());
    EXPECT_FALSE(doc.had_includes());
}

TEST_F(DocumentFiles, OpenReadsModelAndGroupAtNavigates) {
    const auto file = write("doc.json", sample_config);
    const atp::studio::document doc = atp::studio::document::open(file);

    ASSERT_NE(doc.group_at(""), nullptr);  // the root
    ASSERT_NE(doc.group_at("left"), nullptr);
    EXPECT_EQ(doc.group_at("left")->name, "left");
    EXPECT_EQ(doc.group_at("left.src"), nullptr);  // a module is not a group
    EXPECT_EQ(doc.group_at("ghost"), nullptr);
    EXPECT_FALSE(doc.had_includes());
}

TEST_F(DocumentFiles, OpenRejectsInvalidConfigWithAggregatedErrors) {
    const auto file = write("bad.json", R"({"version": "2.0", "pipeline": {"typo": 1}})");
    try {
        (void)atp::studio::document::open(file);
        FAIL() << "expected config_error";
    } catch (const atp::runtime::config_error& e) {
        EXPECT_NE(std::string(e.what()).find("typo"), std::string::npos);  // the validator's text came through
    }
}

TEST_F(DocumentFiles, OpenFlagsIncludes) {
    write("part.json", R"({"group": "g", "modules": []})");
    const auto file = write("doc.json", R"({
        "version": "2.0",
        "pipeline": {"modules": [{"$include": "part.json"}]}
    })");
    const atp::studio::document doc = atp::studio::document::open(file);
    EXPECT_TRUE(doc.had_includes());        // saving would flatten it, so warn
    ASSERT_NE(doc.group_at("g"), nullptr);  // the document opened expanded
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
    EXPECT_TRUE(atp::runtime::validate(written).empty());  // the file is usable by atp_app

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
    doc.add_module("right", "printer");  // the default name is the factory name
    doc.set_expose_input("right", "in", "printer.value");
    doc.connect("", "left.out", "right.in", true);
    doc.add_thread("main_loop", atp::thread_mode::throttled, std::chrono::milliseconds(5));
    doc.set_assignment("left", "main_loop");
    doc.add_plugin("libdemo.dll");
    doc.add_plugin("libdemo.dll");  // a duplicate is not an operation and does not grow undo

    const atp::runtime::config& cfg = doc.config();
    ASSERT_EQ(cfg.pipeline.modules.size(), 2u);
    EXPECT_EQ(cfg.pipeline.modules[1].group->modules[0].module->name, "printer");
    ASSERT_EQ(cfg.pipeline.connections.size(), 1u);
    EXPECT_TRUE(cfg.pipeline.connections[0].replay);
    ASSERT_EQ(cfg.threads.size(), 1u);
    ASSERT_EQ(cfg.assignments.size(), 1u);
    ASSERT_EQ(cfg.plugins.size(), 1u);
    // what the operations built passes the full validate — the invariants are the same
    EXPECT_TRUE(atp::runtime::validate(atp::runtime::encode(cfg)).empty());
}

TEST(DocumentEdit, RejectsInvariantViolationsWithoutPollutingUndo) {
    atp::studio::document doc = atp::studio::document::create();
    doc.add_group("", "g");
    doc.add_module("g", "counter");

    EXPECT_THROW(doc.add_module("g", "counter"), atp::runtime::config_error);      // duplicate name
    EXPECT_THROW(doc.add_module("ghost", "counter"), atp::runtime::config_error);  // no such group
    EXPECT_THROW(doc.add_group("g", "bad.name"), atp::runtime::config_error);      // a dot in the name
    EXPECT_THROW(doc.connect("g", "no_dot", "counter.in"), atp::runtime::config_error);
    EXPECT_THROW(doc.connect("g", "ghost.out", "counter.in"), atp::runtime::config_error);       // no such child
    EXPECT_THROW(doc.add_thread("t", atp::thread_mode::throttled), atp::runtime::config_error);  // no period
    EXPECT_THROW(doc.set_assignment("g", "nowhere"), atp::runtime::config_error);                // no such thread

    // after the refusals exactly the two successful operations roll back, and nothing else
    EXPECT_TRUE(doc.undo());
    EXPECT_TRUE(doc.undo());
    EXPECT_FALSE(doc.can_undo());
    EXPECT_TRUE(doc.config().pipeline.modules.empty());
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
    EXPECT_TRUE(doc.group_at("g")->connections.empty());     // the "a." connection is gone
    EXPECT_TRUE(doc.group_at("g")->expose_outputs.empty());  // and so is the export
    EXPECT_EQ(doc.position("g.a"), std::nullopt);

    doc.remove_child("", "g");                      // the whole subgroup
    EXPECT_TRUE(doc.config().assignments.empty());  // the layout was not orphaned
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
    doc.rename_child("", "g", "stage");  // the layout path follows too
    EXPECT_EQ(doc.config().assignments[0].first, "stage");
}

TEST(DocumentEdit, UndoRedoWalkHistory) {
    atp::studio::document doc = atp::studio::document::create();
    EXPECT_FALSE(doc.can_undo());
    doc.add_group("", "g");
    doc.add_module("g", "m");
    EXPECT_TRUE(doc.undo());  // the module was rolled back
    EXPECT_TRUE(doc.group_at("g")->modules.empty());
    EXPECT_TRUE(doc.redo());  // and brought back
    EXPECT_EQ(doc.group_at("g")->modules.size(), 1u);
    EXPECT_TRUE(doc.undo());
    doc.add_module("g", "other");  // a new branch of the history
    EXPECT_FALSE(doc.can_redo());  // redo was cleared
}

TEST(DocumentEdit, SetPropertyAddsAndReplaces) {
    auto doc = atp::studio::document::create();
    doc.add_module("", "counter");
    doc.set_property("", "counter", "limit", 5);
    ASSERT_EQ(doc.config().pipeline.modules[0].module->properties.size(), 1u);
    EXPECT_EQ(doc.config().pipeline.modules[0].module->properties[0].second, nlohmann::json(5));
    doc.set_property("", "counter", "limit", 7);  // replaced, not duplicated
    ASSERT_EQ(doc.config().pipeline.modules[0].module->properties.size(), 1u);
    EXPECT_EQ(doc.config().pipeline.modules[0].module->properties[0].second, nlohmann::json(7));
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
    EXPECT_TRUE(doc.config().pipeline.modules[0].module->properties.empty());
    doc.clear_property("", "counter", "ghost");  // a missing entry is a no-op, not an error
}

// Changing the pacing of a thread must not cost it its assignments: that is why set_thread exists
// instead of a remove/add pair, which would silently drop them.
TEST(DocumentEdit, SetThreadKeepsAssignments) {
    auto doc = atp::studio::document::create();
    doc.add_thread("worker", atp::thread_mode::on_demand);
    doc.add_group("", "inner");
    doc.set_assignment("inner", "worker");

    doc.set_thread("worker", atp::thread_mode::throttled, std::chrono::milliseconds{250});

    ASSERT_EQ(doc.config().threads.size(), 1u);
    EXPECT_EQ(doc.config().threads[0].mode, atp::thread_mode::throttled);
    EXPECT_EQ(doc.config().threads[0].period, std::chrono::milliseconds{250});
    ASSERT_EQ(doc.config().assignments.size(), 1u);
    EXPECT_EQ(doc.config().assignments[0].second, "worker");
}

TEST(DocumentEdit, SetThreadRejectsContradictoryPeriod) {
    auto doc = atp::studio::document::create();
    doc.add_thread("worker", atp::thread_mode::throttled, std::chrono::milliseconds{100});

    EXPECT_THROW(doc.set_thread("worker", atp::thread_mode::throttled, std::chrono::milliseconds{0}),
                 atp::runtime::config_error);
    EXPECT_THROW(doc.set_thread("worker", atp::thread_mode::on_demand, std::chrono::milliseconds{5}),
                 atp::runtime::config_error);
    EXPECT_THROW(doc.set_thread("missing", atp::thread_mode::on_demand), atp::runtime::config_error);
    // A rejected change leaves the thread as it was.
    EXPECT_EQ(doc.config().threads[0].period, std::chrono::milliseconds{100});
}

TEST(DocumentEdit, SetThreadIsUndoable) {
    auto doc = atp::studio::document::create();
    doc.add_thread("worker", atp::thread_mode::on_demand);

    doc.set_thread("worker", atp::thread_mode::spinning);
    EXPECT_EQ(doc.config().threads[0].mode, atp::thread_mode::spinning);

    EXPECT_TRUE(doc.undo());
    EXPECT_EQ(doc.config().threads[0].mode, atp::thread_mode::on_demand);
}

}  // namespace
