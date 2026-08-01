#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atp/runtime/config_validator.hpp>
#include <atp/studio/project.hpp>

namespace {

class ProjectFiles : public ::testing::Test {
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

TEST_F(ProjectFiles, CreateStartsEmptyWithCurrentSchema) {
    const atp::studio::project proj = atp::studio::project::create();
    EXPECT_EQ(proj.config().schema, atp::runtime::config_schema_version);
    EXPECT_TRUE(proj.config().pipeline.modules.empty());
    EXPECT_FALSE(proj.had_includes());
}

TEST_F(ProjectFiles, OpenReadsModelAndGroupAtNavigates) {
    const auto file = write("proj.json", sample_config);
    const atp::studio::project proj = atp::studio::project::open(file);

    ASSERT_NE(proj.group_at(""), nullptr);
    ASSERT_NE(proj.group_at("left"), nullptr);
    EXPECT_EQ(proj.group_at("left")->name, "left");
    EXPECT_EQ(proj.group_at("left.src"), nullptr);
    EXPECT_EQ(proj.group_at("ghost"), nullptr);
    EXPECT_FALSE(proj.had_includes());
}

TEST_F(ProjectFiles, OpenRejectsInvalidConfigWithAggregatedErrors) {
    const auto file = write("bad.json", R"({"version": "2.0", "pipeline": {"typo": 1}})");
    try {
        (void)atp::studio::project::open(file);
        FAIL() << "expected config_error";
    } catch (const atp::runtime::config_error& e) {
        EXPECT_NE(std::string(e.what()).find("typo"), std::string::npos);
    }
}

TEST_F(ProjectFiles, OpenFlagsIncludes) {
    write("part.json", R"({"group": "g", "modules": []})");
    const auto file = write("proj.json", R"({
        "version": "2.0",
        "pipeline": {"modules": [{"$include": "part.json"}]}
    })");
    const atp::studio::project proj = atp::studio::project::open(file);
    EXPECT_TRUE(proj.had_includes());
    ASSERT_NE(proj.group_at("g"), nullptr);
}

TEST_F(ProjectFiles, SaveWritesValidConfigAndLayoutSidecar) {
    const auto file = write("proj.json", sample_config);
    atp::studio::project proj = atp::studio::project::open(file);
    proj.set_position("left", {10.0f, 20.0f});
    proj.set_position("left.src", {30.0f, 40.0f});

    const auto saved = dir_ / "out.json";
    proj.save(saved);

    std::ifstream in(saved);
    const nlohmann::json written = nlohmann::json::parse(in);
    EXPECT_TRUE(atp::runtime::validate(written).empty());

    const atp::studio::project reopened = atp::studio::project::open(saved);
    ASSERT_TRUE(reopened.position("left.src").has_value());
    EXPECT_EQ(*reopened.position("left.src"), (atp::studio::node_position{30.0f, 40.0f}));
    EXPECT_EQ(reopened.position("ghost"), std::nullopt);
}

TEST(ProjectEdit, BuildsPipelineThroughOperations) {
    atp::studio::project proj = atp::studio::project::create();
    proj.add_group("", "left");
    proj.add_module("left", "counter", "ticks", atp::version{1, 0});
    proj.set_expose_output("left", "out", "ticks.count");
    proj.add_group("", "right");
    proj.add_module("right", "printer");
    proj.set_expose_input("right", "in", "printer.value");
    proj.connect("", "left.out", "right.in", true);
    proj.add_thread("main_loop", atp::thread_mode::throttled, std::chrono::milliseconds(5));
    proj.set_assignment("left", "main_loop");
    proj.add_plugin("libdemo.dll");
    proj.add_plugin("libdemo.dll");

    const atp::runtime::config& cfg = proj.config();
    ASSERT_EQ(cfg.pipeline.modules.size(), 2u);
    EXPECT_EQ(cfg.pipeline.modules[1].group->modules[0].module->name, "printer");
    ASSERT_EQ(cfg.pipeline.connections.size(), 1u);
    EXPECT_TRUE(cfg.pipeline.connections[0].replay);
    ASSERT_EQ(cfg.threads.size(), 1u);
    ASSERT_EQ(cfg.assignments.size(), 1u);
    ASSERT_EQ(cfg.plugins.size(), 1u);
    EXPECT_TRUE(atp::runtime::validate(atp::runtime::encode(cfg)).empty());
}

TEST(ProjectEdit, RejectsInvariantViolationsWithoutPollutingUndo) {
    atp::studio::project proj = atp::studio::project::create();
    proj.add_group("", "g");
    proj.add_module("g", "counter");

    EXPECT_THROW(proj.add_module("g", "counter"), atp::runtime::config_error);
    EXPECT_THROW(proj.add_module("ghost", "counter"), atp::runtime::config_error);
    EXPECT_THROW(proj.add_group("g", "bad.name"), atp::runtime::config_error);
    EXPECT_THROW(proj.connect("g", "no_dot", "counter.in"), atp::runtime::config_error);
    EXPECT_THROW(proj.connect("g", "ghost.out", "counter.in"), atp::runtime::config_error);
    EXPECT_THROW(proj.add_thread("t", atp::thread_mode::throttled), atp::runtime::config_error);
    EXPECT_THROW(proj.set_assignment("g", "nowhere"), atp::runtime::config_error);

    EXPECT_TRUE(proj.undo());
    EXPECT_TRUE(proj.undo());
    EXPECT_FALSE(proj.can_undo());
    EXPECT_TRUE(proj.config().pipeline.modules.empty());
}

TEST(ProjectEdit, RemoveChildCleansReferences) {
    atp::studio::project proj = atp::studio::project::create();
    proj.add_group("", "g");
    proj.add_module("g", "a");
    proj.add_module("g", "b");
    proj.connect("g", "a.out", "b.in");
    proj.set_expose_output("g", "alias", "a.out");
    proj.add_thread("t", atp::thread_mode::on_demand);
    proj.set_assignment("g", "t");
    proj.set_position("g.a", {1.0f, 2.0f});

    proj.remove_child("g", "a");
    EXPECT_TRUE(proj.group_at("g")->connections.empty());
    EXPECT_TRUE(proj.group_at("g")->expose_outputs.empty());
    EXPECT_EQ(proj.position("g.a"), std::nullopt);

    proj.remove_child("", "g");
    EXPECT_TRUE(proj.config().assignments.empty());
}

TEST(ProjectEdit, RenameChildRewritesReferences) {
    atp::studio::project proj = atp::studio::project::create();
    proj.add_group("", "g");
    proj.add_module("g", "a");
    proj.add_module("g", "b");
    proj.connect("g", "a.out", "b.in");
    proj.set_expose_output("g", "alias", "a.out");
    proj.set_position("g.a", {1.0f, 2.0f});

    proj.rename_child("g", "a", "producer");
    EXPECT_EQ(proj.group_at("g")->connections[0].from, "producer.out");
    EXPECT_EQ(proj.group_at("g")->expose_outputs[0].second, "producer.out");
    EXPECT_EQ(proj.position("g.producer"), (atp::studio::node_position{1.0f, 2.0f}));
    EXPECT_EQ(proj.position("g.a"), std::nullopt);

    proj.add_thread("t", atp::thread_mode::on_demand);
    proj.set_assignment("g", "t");
    proj.rename_child("", "g", "stage");
    EXPECT_EQ(proj.config().assignments[0].first, "stage");
}

TEST(ProjectEdit, UndoRedoWalkHistory) {
    atp::studio::project proj = atp::studio::project::create();
    EXPECT_FALSE(proj.can_undo());
    proj.add_group("", "g");
    proj.add_module("g", "m");
    EXPECT_TRUE(proj.undo());
    EXPECT_TRUE(proj.group_at("g")->modules.empty());
    EXPECT_TRUE(proj.redo());
    EXPECT_EQ(proj.group_at("g")->modules.size(), 1u);
    EXPECT_TRUE(proj.undo());
    proj.add_module("g", "other");
    EXPECT_FALSE(proj.can_redo());
}

TEST(ProjectEdit, SetPropertyAddsAndReplaces) {
    auto proj = atp::studio::project::create();
    proj.add_module("", "counter");
    proj.set_property("", "counter", "limit", 5);
    ASSERT_EQ(proj.config().pipeline.modules[0].module->properties.size(), 1u);
    EXPECT_EQ(proj.config().pipeline.modules[0].module->properties[0].second, nlohmann::json(5));
    proj.set_property("", "counter", "limit", 7);
    ASSERT_EQ(proj.config().pipeline.modules[0].module->properties.size(), 1u);
    EXPECT_EQ(proj.config().pipeline.modules[0].module->properties[0].second, nlohmann::json(7));
    EXPECT_TRUE(proj.can_undo());
}

TEST(ProjectEdit, SetPropertyRejectsNonScalarAndGhostModule) {
    auto proj = atp::studio::project::create();
    proj.add_module("", "counter");
    EXPECT_THROW(proj.set_property("", "counter", "limit", nlohmann::json::object()), atp::runtime::config_error);
    EXPECT_THROW(proj.set_property("", "ghost", "limit", 5), atp::runtime::config_error);
}

TEST(ProjectEdit, ClearPropertyRemovesPair) {
    auto proj = atp::studio::project::create();
    proj.add_module("", "counter");
    proj.set_property("", "counter", "limit", 5);
    proj.clear_property("", "counter", "limit");
    EXPECT_TRUE(proj.config().pipeline.modules[0].module->properties.empty());
    proj.clear_property("", "counter", "ghost");
}

TEST(ProjectEdit, SetThreadKeepsAssignments) {
    auto proj = atp::studio::project::create();
    proj.add_thread("worker", atp::thread_mode::on_demand);
    proj.add_group("", "inner");
    proj.set_assignment("inner", "worker");

    proj.set_thread("worker", atp::thread_mode::throttled, std::chrono::milliseconds{250});

    ASSERT_EQ(proj.config().threads.size(), 1u);
    EXPECT_EQ(proj.config().threads[0].mode, atp::thread_mode::throttled);
    EXPECT_EQ(proj.config().threads[0].period, std::chrono::milliseconds{250});
    ASSERT_EQ(proj.config().assignments.size(), 1u);
    EXPECT_EQ(proj.config().assignments[0].second, "worker");
}

TEST(ProjectEdit, SetThreadRejectsContradictoryPeriod) {
    auto proj = atp::studio::project::create();
    proj.add_thread("worker", atp::thread_mode::throttled, std::chrono::milliseconds{100});

    EXPECT_THROW(proj.set_thread("worker", atp::thread_mode::throttled, std::chrono::milliseconds{0}),
                 atp::runtime::config_error);
    EXPECT_THROW(proj.set_thread("worker", atp::thread_mode::on_demand, std::chrono::milliseconds{5}),
                 atp::runtime::config_error);
    EXPECT_THROW(proj.set_thread("missing", atp::thread_mode::on_demand), atp::runtime::config_error);
    EXPECT_EQ(proj.config().threads[0].period, std::chrono::milliseconds{100});
}

TEST(ProjectEdit, SetThreadIsUndoable) {
    auto proj = atp::studio::project::create();
    proj.add_thread("worker", atp::thread_mode::on_demand);

    proj.set_thread("worker", atp::thread_mode::spinning);
    EXPECT_EQ(proj.config().threads[0].mode, atp::thread_mode::spinning);

    EXPECT_TRUE(proj.undo());
    EXPECT_EQ(proj.config().threads[0].mode, atp::thread_mode::on_demand);
}

TEST(ProjectModified, NewProjectIsClean) {
    const atp::studio::project proj = atp::studio::project::create();
    EXPECT_FALSE(proj.is_modified());
}

TEST(ProjectModified, EditMarksModified) {
    atp::studio::project proj = atp::studio::project::create();
    proj.add_group("", "g");
    EXPECT_TRUE(proj.is_modified());
}

TEST(ProjectModified, RejectedOperationLeavesProjectClean) {
    atp::studio::project proj = atp::studio::project::create();
    EXPECT_THROW(proj.add_module("ghost", "counter"), atp::runtime::config_error);
    EXPECT_FALSE(proj.is_modified());
}

TEST_F(ProjectFiles, SaveClearsModified) {
    atp::studio::project proj = atp::studio::project::create();
    proj.add_group("", "g");
    ASSERT_TRUE(proj.is_modified());

    proj.save(dir_ / "proj.json");
    EXPECT_FALSE(proj.is_modified());
}

TEST_F(ProjectFiles, OpenStartsClean) {
    const auto file = write("proj.json", sample_config);
    const atp::studio::project proj = atp::studio::project::open(file);
    EXPECT_FALSE(proj.is_modified());
}

TEST_F(ProjectFiles, UndoBackToSavedStateClearsModified) {
    atp::studio::project proj = atp::studio::project::create();
    proj.add_group("", "g");
    proj.save(dir_ / "proj.json");

    proj.add_module("g", "counter");
    ASSERT_TRUE(proj.is_modified());
    ASSERT_TRUE(proj.undo());
    EXPECT_FALSE(proj.is_modified());
}

TEST_F(ProjectFiles, UndoThenDifferentEditStaysModified) {
    atp::studio::project proj = atp::studio::project::create();
    proj.add_group("", "g");
    proj.add_module("g", "counter");
    proj.save(dir_ / "proj.json");

    ASSERT_TRUE(proj.undo());
    proj.add_module("g", "printer");
    EXPECT_TRUE(proj.is_modified());
}

TEST(ProjectMove, MovesAModuleWithItsProperties) {
    atp::studio::project proj = atp::studio::project::create();
    proj.add_group("", "a");
    proj.add_group("", "b");
    proj.add_module("a", "counter", "src");
    proj.set_property("a", "src", "limit", 5);

    const atp::studio::move_result r = proj.move_child("a", "src", "b");
    EXPECT_EQ(r.new_name, "src");
    EXPECT_EQ(r.dropped_connections, 0u);
    EXPECT_EQ(r.dropped_exposes, 0u);

    ASSERT_NE(proj.group_at("a"), nullptr);
    EXPECT_TRUE(proj.group_at("a")->modules.empty());
    ASSERT_NE(proj.group_at("b"), nullptr);
    ASSERT_EQ(proj.group_at("b")->modules.size(), 1u);
    ASSERT_TRUE(proj.group_at("b")->modules.front().module.has_value());
    const atp::runtime::module_node& m = *proj.group_at("b")->modules.front().module;
    EXPECT_EQ(m.name, "src");
    EXPECT_EQ(m.factory, "counter");
    ASSERT_EQ(m.properties.size(), 1u);
    EXPECT_EQ(m.properties.front().first, "limit");
    EXPECT_EQ(m.properties.front().second, 5);
}

TEST(ProjectMove, DropsWhatCannotFollowTheChild) {
    atp::studio::project proj = atp::studio::project::create();
    proj.add_group("", "target");
    proj.add_module("", "counter", "src");
    proj.add_module("", "printer", "dst");
    proj.add_module("", "printer", "other");
    proj.connect("", "src.value", "dst.value");
    proj.connect("", "src.value", "other.value");
    proj.connect("", "dst.done", "other.trigger");
    proj.set_expose_output("", "out", "src.value");
    proj.set_expose_input("", "in", "dst.value");

    const atp::studio::move_result r = proj.move_child("", "src", "target");
    EXPECT_EQ(r.dropped_connections, 2u);
    EXPECT_EQ(r.dropped_exposes, 1u);

    ASSERT_EQ(proj.group_at("")->connections.size(), 1u);
    EXPECT_EQ(proj.group_at("")->connections.front().from, "dst.done");
    EXPECT_TRUE(proj.group_at("")->expose_outputs.empty());
    EXPECT_EQ(proj.group_at("")->expose_inputs.size(), 1u);
    ASSERT_EQ(proj.group_at("target")->modules.size(), 1u);
}

TEST(ProjectMove, SuffixesANameTakenInTheTargetGroup) {
    atp::studio::project proj = atp::studio::project::create();
    proj.add_group("", "a");
    proj.add_group("", "b");
    proj.add_module("a", "counter", "src");
    proj.add_module("b", "printer", "src");

    const atp::studio::move_result r = proj.move_child("a", "src", "b");
    EXPECT_EQ(r.new_name, "src_2");
    ASSERT_EQ(proj.group_at("b")->modules.size(), 2u);
    ASSERT_TRUE(proj.group_at("b")->modules.at(1).module.has_value());
    EXPECT_EQ(proj.group_at("b")->modules.at(1).module->name, "src_2");
    EXPECT_EQ(proj.group_at("b")->modules.at(1).module->factory, "counter");
}

TEST(ProjectMove, MovingAGroupRewritesAssignmentsAndPositionsOfItsSubtree) {
    atp::studio::project proj = atp::studio::project::create();
    proj.add_group("", "stage");
    proj.add_group("", "outer");
    proj.add_module("stage", "counter", "src");
    proj.add_thread("t", atp::thread_mode::on_demand);
    proj.set_assignment("stage", "t");
    proj.set_position("stage", {1.0f, 2.0f});
    proj.set_position("stage.src", {3.0f, 4.0f});

    const atp::studio::move_result r = proj.move_child("", "stage", "outer");
    EXPECT_EQ(r.new_name, "stage");

    EXPECT_EQ(proj.group_at("stage"), nullptr);
    ASSERT_NE(proj.group_at("outer.stage"), nullptr);
    ASSERT_EQ(proj.group_at("outer.stage")->modules.size(), 1u);

    ASSERT_EQ(proj.config().assignments.size(), 1u);
    EXPECT_EQ(proj.config().assignments.front().first, "outer.stage");
    EXPECT_EQ(proj.config().assignments.front().second, "t");

    EXPECT_FALSE(proj.position("stage").has_value());
    ASSERT_TRUE(proj.position("outer.stage").has_value());
    EXPECT_EQ(proj.position("outer.stage")->x, 1.0f);
    ASSERT_TRUE(proj.position("outer.stage.src").has_value());
    EXPECT_EQ(proj.position("outer.stage.src")->y, 4.0f);
}

TEST(ProjectMove, RefusesImpossibleMoves) {
    atp::studio::project proj = atp::studio::project::create();
    proj.add_group("", "a");
    proj.add_group("a", "inner");
    proj.add_module("", "counter", "src");

    EXPECT_THROW((void)proj.move_child("ghost", "src", "a"), atp::runtime::config_error);
    EXPECT_THROW((void)proj.move_child("", "ghost", "a"), atp::runtime::config_error);
    EXPECT_THROW((void)proj.move_child("", "src", "ghost"), atp::runtime::config_error);
    EXPECT_THROW((void)proj.move_child("", "src", ""), atp::runtime::config_error);
    EXPECT_THROW((void)proj.move_child("", "a", "a"), atp::runtime::config_error);
    EXPECT_THROW((void)proj.move_child("", "a", "a.inner"), atp::runtime::config_error);

    EXPECT_EQ(proj.group_at("")->modules.size(), 2u);
    EXPECT_NE(proj.group_at("a.inner"), nullptr);
}

TEST(ProjectMove, ARefusedMoveLeavesNoHistoryBehind) {
    atp::studio::project proj = atp::studio::project::create();
    proj.add_group("", "a");
    proj.add_module("", "counter", "src");

    EXPECT_THROW((void)proj.move_child("", "ghost", "a"), atp::runtime::config_error);

    ASSERT_TRUE(proj.undo());
    ASSERT_EQ(proj.group_at("")->modules.size(), 1u);
    EXPECT_TRUE(proj.group_at("")->modules.front().group != nullptr);
}

TEST(ProjectMove, UndoRestoresTheNodeAndItsConnections) {
    atp::studio::project proj = atp::studio::project::create();
    proj.add_group("", "target");
    proj.add_module("", "counter", "src");
    proj.add_module("", "printer", "dst");
    proj.connect("", "src.value", "dst.value");

    (void)proj.move_child("", "src", "target");
    ASSERT_TRUE(proj.undo());

    EXPECT_TRUE(proj.group_at("target")->modules.empty());
    EXPECT_EQ(proj.group_at("")->modules.size(), 3u);
    ASSERT_EQ(proj.group_at("")->connections.size(), 1u);
    EXPECT_EQ(proj.group_at("")->connections.front().from, "src.value");
}

TEST(ProjectCopy, CopiesAModuleWithItsPropertiesAndLeavesTheOriginal) {
    atp::studio::project proj = atp::studio::project::create();
    proj.add_group("", "a");
    proj.add_group("", "b");
    proj.add_module("a", "counter", "src");
    proj.set_property("a", "src", "limit", 5);

    EXPECT_EQ(proj.copy_child("a", "src", "b"), "src");

    ASSERT_EQ(proj.group_at("a")->modules.size(), 1u);
    ASSERT_EQ(proj.group_at("b")->modules.size(), 1u);
    const atp::runtime::module_node& m = *proj.group_at("b")->modules.front().module;
    EXPECT_EQ(m.name, "src");
    EXPECT_EQ(m.factory, "counter");
    ASSERT_EQ(m.properties.size(), 1u);
    EXPECT_EQ(m.properties.front().second, 5);
}

TEST(ProjectCopy, IntoTheSameGroupDuplicatesUnderASuffixedName) {
    atp::studio::project proj = atp::studio::project::create();
    proj.add_module("", "counter", "src");

    EXPECT_EQ(proj.copy_child("", "src", ""), "src_2");

    ASSERT_EQ(proj.group_at("")->modules.size(), 2u);
    EXPECT_EQ(proj.group_at("")->modules.at(0).module->name, "src");
    EXPECT_EQ(proj.group_at("")->modules.at(1).module->name, "src_2");
}

TEST(ProjectCopy, LeavesTheSourceGroupsConnectionsAlone) {
    atp::studio::project proj = atp::studio::project::create();
    proj.add_group("", "target");
    proj.add_module("", "counter", "src");
    proj.add_module("", "printer", "dst");
    proj.connect("", "src.value", "dst.value");
    proj.set_expose_output("", "out", "src.value");

    (void)proj.copy_child("", "src", "target");

    ASSERT_EQ(proj.group_at("")->connections.size(), 1u);
    EXPECT_EQ(proj.group_at("")->expose_outputs.size(), 1u);
    EXPECT_TRUE(proj.group_at("target")->connections.empty());
}

TEST(ProjectCopy, CopyingAGroupReproducesItsSubtreeAndInnerWiring) {
    atp::studio::project proj = atp::studio::project::create();
    proj.add_group("", "stage");
    proj.add_group("", "outer");
    proj.add_module("stage", "counter", "src");
    proj.add_module("stage", "printer", "dst");
    proj.connect("stage", "src.value", "dst.value");
    proj.set_expose_input("stage", "in", "dst.value");

    EXPECT_EQ(proj.copy_child("", "stage", "outer"), "stage");

    const atp::runtime::group_node* copy = proj.group_at("outer.stage");
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->modules.size(), 2u);
    ASSERT_EQ(copy->connections.size(), 1u);
    EXPECT_EQ(copy->connections.front().from, "src.value");
    ASSERT_EQ(copy->expose_inputs.size(), 1u);
    EXPECT_EQ(copy->expose_inputs.front().first, "in");
}

TEST(ProjectCopy, TheCopyIsIndependentOfTheOriginal) {
    atp::studio::project proj = atp::studio::project::create();
    proj.add_group("", "stage");
    proj.add_group("", "outer");
    proj.add_module("stage", "counter", "src");

    (void)proj.copy_child("", "stage", "outer");
    proj.set_property("stage", "src", "limit", 7);

    ASSERT_NE(proj.group_at("outer.stage"), nullptr);
    EXPECT_TRUE(proj.group_at("outer.stage")->modules.front().module->properties.empty());
    EXPECT_EQ(proj.group_at("stage")->modules.front().module->properties.size(), 1u);
}

TEST(ProjectCopy, DuplicatesAssignmentsAndPositionsOfTheSubtree) {
    atp::studio::project proj = atp::studio::project::create();
    proj.add_group("", "stage");
    proj.add_group("", "outer");
    proj.add_module("stage", "counter", "src");
    proj.add_thread("t", atp::thread_mode::on_demand);
    proj.set_assignment("stage", "t");
    proj.set_position("stage", {1.0f, 2.0f});
    proj.set_position("stage.src", {3.0f, 4.0f});

    (void)proj.copy_child("", "stage", "outer");

    ASSERT_EQ(proj.config().assignments.size(), 2u);
    EXPECT_EQ(proj.config().assignments.at(0).first, "stage");
    EXPECT_EQ(proj.config().assignments.at(1).first, "outer.stage");
    EXPECT_EQ(proj.config().assignments.at(1).second, "t");

    ASSERT_TRUE(proj.position("stage").has_value());
    ASSERT_TRUE(proj.position("outer.stage").has_value());
    EXPECT_EQ(proj.position("outer.stage")->x, 1.0f);
    ASSERT_TRUE(proj.position("outer.stage.src").has_value());
    EXPECT_EQ(proj.position("outer.stage.src")->y, 4.0f);
}

TEST(ProjectCopy, CopyingAGroupIntoItsOwnSubtreeNestsASnapshot) {
    atp::studio::project proj = atp::studio::project::create();
    proj.add_group("", "a");
    proj.add_group("a", "inner");

    EXPECT_EQ(proj.copy_child("", "a", "a.inner"), "a");

    ASSERT_NE(proj.group_at("a.inner.a"), nullptr);
    ASSERT_NE(proj.group_at("a.inner.a.inner"), nullptr);
    EXPECT_EQ(proj.group_at("a.inner.a.inner")->modules.size(), 0u);
}

TEST(ProjectCopy, RefusesMissingGroupsAndChildrenWithoutHistory) {
    atp::studio::project proj = atp::studio::project::create();
    proj.add_group("", "a");
    proj.add_module("", "counter", "src");

    EXPECT_THROW((void)proj.copy_child("ghost", "src", "a"), atp::runtime::config_error);
    EXPECT_THROW((void)proj.copy_child("", "ghost", "a"), atp::runtime::config_error);
    EXPECT_THROW((void)proj.copy_child("", "src", "ghost"), atp::runtime::config_error);

    ASSERT_TRUE(proj.undo());
    ASSERT_EQ(proj.group_at("")->modules.size(), 1u);
    EXPECT_TRUE(proj.group_at("")->modules.front().group != nullptr);
}

TEST(ProjectCopy, UndoRemovesTheCopy) {
    atp::studio::project proj = atp::studio::project::create();
    proj.add_group("", "target");
    proj.add_module("", "counter", "src");

    (void)proj.copy_child("", "src", "target");
    ASSERT_TRUE(proj.undo());

    EXPECT_TRUE(proj.group_at("target")->modules.empty());
    EXPECT_EQ(proj.group_at("")->modules.size(), 2u);
}

/// root → g1 → g2 → g3 with the module in g3, its output re-exported at every level and consumed at
/// the root. Three levels are the shortest shape where an alias has a re-export above it that in
/// turn has one above itself, which is what the cascade has to walk.
atp::studio::project nested_chain() {
    atp::studio::project proj = atp::studio::project::create();
    proj.add_group("", "g1");
    proj.add_group("g1", "g2");
    proj.add_group("g1.g2", "g3");
    proj.add_module("g1.g2.g3", "counter", "src");
    proj.add_module("", "printer", "sink");
    proj.set_expose_output("g1.g2.g3", "count", "src.value");
    proj.set_expose_output("g1.g2", "count", "g3.count");
    proj.set_expose_output("g1", "count", "g2.count");
    proj.connect("", "g1.count", "sink.value");
    return proj;
}

TEST(ProjectExpose, RemovingAnAliasCascadesUpTheChain) {
    atp::studio::project proj = nested_chain();

    proj.remove_expose_output("g1.g2.g3", "count");

    EXPECT_TRUE(proj.group_at("g1.g2.g3")->expose_outputs.empty());
    EXPECT_TRUE(proj.group_at("g1.g2")->expose_outputs.empty());
    EXPECT_TRUE(proj.group_at("g1")->expose_outputs.empty());
    EXPECT_TRUE(proj.group_at("")->connections.empty());
}

TEST(ProjectExpose, RemovingTheDeepModuleCascadesUpTheChain) {
    atp::studio::project proj = nested_chain();

    proj.remove_child("g1.g2.g3", "src");

    EXPECT_TRUE(proj.group_at("g1.g2.g3")->expose_outputs.empty());
    EXPECT_TRUE(proj.group_at("g1.g2")->expose_outputs.empty());
    EXPECT_TRUE(proj.group_at("g1")->expose_outputs.empty());
    EXPECT_TRUE(proj.group_at("")->connections.empty());
}

TEST(ProjectExpose, MovingTheDeepModuleReportsTheWholeCascade) {
    atp::studio::project proj = nested_chain();

    const atp::studio::move_result r = proj.move_child("g1.g2.g3", "src", "");

    EXPECT_EQ(r.dropped_exposes, 3u);
    EXPECT_EQ(r.dropped_connections, 1u);
    EXPECT_TRUE(proj.group_at("g1")->expose_outputs.empty());
    EXPECT_TRUE(proj.group_at("")->connections.empty());
}

TEST(ProjectExpose, UndoRestoresTheWholeCascadeAtOnce) {
    atp::studio::project proj = nested_chain();

    proj.remove_expose_output("g1.g2.g3", "count");
    ASSERT_TRUE(proj.undo());

    EXPECT_EQ(proj.group_at("g1.g2.g3")->expose_outputs.size(), 1u);
    EXPECT_EQ(proj.group_at("g1.g2")->expose_outputs.size(), 1u);
    EXPECT_EQ(proj.group_at("g1")->expose_outputs.size(), 1u);
    EXPECT_EQ(proj.group_at("")->connections.size(), 1u);
}

TEST(ProjectExpose, ACascadeStopsAtWhatItDoesNotName) {
    atp::studio::project proj = nested_chain();
    proj.add_module("g1.g2.g3", "counter", "other");
    proj.set_expose_output("g1.g2.g3", "spare", "other.value");
    proj.set_expose_output("g1.g2", "spare", "g3.spare");

    proj.remove_expose_output("g1.g2.g3", "count");

    ASSERT_EQ(proj.group_at("g1.g2.g3")->expose_outputs.size(), 1u);
    EXPECT_EQ(proj.group_at("g1.g2.g3")->expose_outputs.front().first, "spare");
    ASSERT_EQ(proj.group_at("g1.g2")->expose_outputs.size(), 1u);
    EXPECT_EQ(proj.group_at("g1.g2")->expose_outputs.front().first, "spare");
}

TEST(ProjectExpose, RefusesAPathIntoASubgroupThatExportsNoSuchPort) {
    atp::studio::project proj = atp::studio::project::create();
    proj.add_group("", "g");
    proj.add_group("g", "inner");
    proj.add_module("g.inner", "counter", "c");
    proj.add_module("", "printer", "sink");

    EXPECT_THROW(proj.set_expose_output("g", "out", "inner.count"), atp::runtime::config_error);
    EXPECT_THROW(proj.connect("", "g.count", "sink.value"), atp::runtime::config_error);

    proj.set_expose_output("g.inner", "count", "c.value");
    EXPECT_THROW(proj.set_expose_input("g", "in", "inner.count"), atp::runtime::config_error);
    EXPECT_NO_THROW(proj.set_expose_output("g", "out", "inner.count"));
}

TEST(ProjectExpose, LeavesAModulesPortsToTheRuntime) {
    atp::studio::project proj = atp::studio::project::create();
    proj.add_module("", "counter", "src");
    proj.add_module("", "printer", "sink");

    EXPECT_NO_THROW(proj.connect("", "src.whatever", "sink.value"));
    EXPECT_NO_THROW(proj.set_expose_output("", "out", "src.whatever"));
}

TEST(ProjectExpose, RenamingAnAliasKeepsTheChainAbove) {
    atp::studio::project proj = nested_chain();

    proj.rename_expose_output("g1.g2", "count", "ticks");
    ASSERT_EQ(proj.group_at("g1.g2")->expose_outputs.size(), 1u);
    EXPECT_EQ(proj.group_at("g1.g2")->expose_outputs.front().first, "ticks");
    EXPECT_EQ(proj.group_at("g1")->expose_outputs.front().second, "g2.ticks");
    EXPECT_EQ(proj.group_at("")->connections.front().from, "g1.count");

    proj.rename_expose_output("g1", "count", "numbers");
    EXPECT_EQ(proj.group_at("")->connections.front().from, "g1.numbers");
}

TEST(ProjectExpose, RenameRefusesAMissingOrTakenAlias) {
    atp::studio::project proj = nested_chain();
    proj.add_module("g1.g2.g3", "counter", "other");
    proj.set_expose_output("g1.g2.g3", "spare", "other.value");

    EXPECT_THROW(proj.rename_expose_output("g1.g2.g3", "ghost", "x"), atp::runtime::config_error);
    EXPECT_THROW(proj.rename_expose_output("g1.g2.g3", "count", "spare"), atp::runtime::config_error);
    EXPECT_THROW(proj.rename_expose_output("g1.g2.g3", "count", "bad.name"), atp::runtime::config_error);
    EXPECT_EQ(proj.group_at("g1.g2")->expose_outputs.front().second, "g3.count");
}

TEST(ProjectEdit, RemoveChildrenIsOneUndoStep) {
    atp::studio::project proj = atp::studio::project::create();
    proj.add_module("", "src", "a");
    proj.add_module("", "mid", "b");
    proj.add_module("", "sink", "c");
    proj.connect("", "a.out", "b.in");

    proj.remove_children("", {"a", "c"});
    ASSERT_EQ(proj.config().pipeline.modules.size(), 1u);
    EXPECT_EQ(proj.config().pipeline.modules.front().module->name, "b");
    EXPECT_TRUE(proj.config().pipeline.connections.empty());

    ASSERT_TRUE(proj.undo());
    EXPECT_EQ(proj.config().pipeline.modules.size(), 3u);
    EXPECT_EQ(proj.config().pipeline.connections.size(), 1u);
}

TEST(ProjectEdit, RemoveChildrenRejectsABadNameBeforeTouchingAnything) {
    atp::studio::project proj = atp::studio::project::create();
    proj.add_module("", "src", "a");
    proj.add_module("", "sink", "b");

    EXPECT_THROW(proj.remove_children("", {"a", "ghost"}), atp::runtime::config_error);
    EXPECT_EQ(proj.config().pipeline.modules.size(), 2u);
}

TEST(ProjectEdit, RemoveChildrenOfNothingIsNotAnOperation) {
    atp::studio::project proj = atp::studio::project::create();
    proj.add_module("", "src", "a");
    const bool undoable = proj.can_undo();
    proj.remove_children("", {});
    EXPECT_EQ(proj.can_undo(), undoable);
    EXPECT_EQ(proj.config().pipeline.modules.size(), 1u);
}

TEST(ProjectEditScope, FoldsSeveralOperationsIntoOneUndoStep) {
    atp::studio::project proj = atp::studio::project::create();
    proj.add_group("", "g");
    {
        const atp::studio::project::edit_scope scope(proj);
        proj.add_module("g", "a");
        proj.add_module("g", "b");
        proj.add_module("g", "c");
    }
    ASSERT_NE(proj.group_at("g"), nullptr);
    ASSERT_EQ(proj.group_at("g")->modules.size(), 3u);

    ASSERT_TRUE(proj.undo());
    ASSERT_NE(proj.group_at("g"), nullptr);
    EXPECT_TRUE(proj.group_at("g")->modules.empty());
}

TEST(ProjectEditScope, NestedScopesJoinTheOutermostOne) {
    atp::studio::project proj = atp::studio::project::create();
    {
        const atp::studio::project::edit_scope outer(proj);
        proj.add_group("", "a");
        {
            const atp::studio::project::edit_scope inner(proj);
            proj.add_group("", "b");
        }
        proj.add_group("", "c");
    }
    ASSERT_EQ(proj.config().pipeline.modules.size(), 3u);

    ASSERT_TRUE(proj.undo());
    EXPECT_TRUE(proj.config().pipeline.modules.empty());
    EXPECT_FALSE(proj.can_undo());
}

TEST(ProjectEditScope, FailedRemoveExposeKeepsTheScopeSnapshot) {
    atp::studio::project proj = atp::studio::project::create();
    proj.add_group("", "g");
    proj.add_module("g", "m");
    proj.set_expose_output("g", "out", "m.value");
    {
        const atp::studio::project::edit_scope scope(proj);
        proj.add_module("g", "extra");
        EXPECT_THROW(proj.remove_expose_output("g", "ghost"), atp::runtime::config_error);
    }
    ASSERT_TRUE(proj.undo());
    ASSERT_NE(proj.group_at("g"), nullptr);
    EXPECT_EQ(proj.group_at("g")->modules.size(), 1u);
    EXPECT_EQ(proj.group_at("g")->expose_outputs.size(), 1u);
}

}  // namespace
