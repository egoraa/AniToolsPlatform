// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <atp/studio/clipboard.hpp>
#include <atp/studio/project.hpp>

#include "support/required.hpp"

namespace {

using atp::runtime::config_error;
using atp::studio::clipboard;
using atp::studio::node_position;
using atp::studio::project;

std::vector<std::string> children_of(const project& proj, const std::string& group_path) {
    std::vector<std::string> names;
    const atp::runtime::group_node* g = proj.group_at(group_path);
    if (g == nullptr) {
        return names;
    }
    for (const atp::runtime::child_node& c : g->modules) {
        names.push_back(c.module ? c.module->name : c.group->name);
    }
    return names;
}

TEST(StudioClipboard, CopyTakesTheInternalConnectionAndLeavesTheOutsideOne) {
    project proj = project::create();
    proj.add_module("", "src", "a");
    proj.add_module("", "mid", "b");
    proj.add_module("", "sink", "c");
    proj.connect("", "a.out", "b.in");
    proj.connect("", "b.out", "c.in");

    const clipboard clip = proj.copy_children("", {"a", "b"});

    EXPECT_EQ(clip.nodes.size(), 2u);
    ASSERT_EQ(clip.connections.size(), 1u);
    EXPECT_EQ(clip.connections.front().from, "a.out");
    EXPECT_EQ(clip.connections.front().to, "b.in");
}

TEST(StudioClipboard, CopyKeepsTheGroupOrderWhateverTheNameOrder) {
    project proj = project::create();
    proj.add_module("", "src", "a");
    proj.add_module("", "mid", "b");
    proj.add_module("", "sink", "c");

    const clipboard clip = proj.copy_children("", {"c", "a"});

    ASSERT_EQ(clip.nodes.size(), 2u);
    EXPECT_EQ(atp_tests::required(clip.nodes.front().node.module).name, "a");
    EXPECT_EQ(atp_tests::required(clip.nodes.back().node.module).name, "c");
}

TEST(StudioClipboard, CopyCarriesSubtreePositionsAndAssignmentsAsRelativePaths) {
    project proj = project::create();
    proj.add_thread("t", atp::thread_mode::on_demand);
    proj.add_group("", "box");
    proj.add_module("box", "src", "a");
    proj.set_position("box", {10.0f, 20.0f});
    proj.set_position("box.a", {30.0f, 40.0f});
    proj.set_assignment("box", "t");

    const clipboard clip = proj.copy_children("", {"box"});

    ASSERT_EQ(clip.nodes.size(), 1u);
    const atp::studio::clip_node& entry = clip.nodes.front();
    ASSERT_TRUE(entry.position.has_value());
    EXPECT_EQ(*entry.position, (node_position{10.0f, 20.0f}));
    ASSERT_EQ(entry.subtree_positions.size(), 1u);
    EXPECT_EQ(entry.subtree_positions.at("a"), (node_position{30.0f, 40.0f}));
    ASSERT_EQ(entry.assignments.size(), 1u);
    EXPECT_EQ(entry.assignments.front().first, "");
    EXPECT_EQ(entry.assignments.front().second, "t");
}

TEST(StudioClipboard, CopyChangesNothingAndPushesNoUndoStep) {
    project proj = project::create();
    proj.add_module("", "src", "a");
    const bool undoable = proj.can_undo();

    const clipboard clip = proj.copy_children("", {"a"});

    EXPECT_FALSE(clip.empty());
    EXPECT_EQ(proj.can_undo(), undoable);
    EXPECT_EQ(children_of(proj, ""), std::vector<std::string>{"a"});
}

TEST(StudioClipboard, CopyRejectsAnUnknownGroupOrName) {
    project proj = project::create();
    proj.add_module("", "src", "a");

    EXPECT_THROW((void)proj.copy_children("nowhere", {"a"}), config_error);
    EXPECT_THROW((void)proj.copy_children("", {"ghost"}), config_error);
}

TEST(StudioClipboard, CopyOfNothingIsAnEmptyClipboard) {
    project proj = project::create();
    proj.add_module("", "src", "a");
    const clipboard clip = proj.copy_children("", {});
    EXPECT_TRUE(clip.empty());
}

TEST(StudioClipboard, PasteCarriesTheModuleWithItsPropertiesIntoAnotherGroup) {
    project proj = project::create();
    proj.add_group("", "src");
    proj.add_group("", "dst");
    proj.add_module("src", "counter", "ticks", atp::version{1, 0});
    proj.set_property("src", "ticks", "step", 5);

    const clipboard clip = proj.copy_children("src", {"ticks"});
    const std::vector<std::string> made = proj.paste("dst", clip, std::nullopt);

    ASSERT_EQ(made, std::vector<std::string>{"ticks"});
    ASSERT_EQ(children_of(proj, "dst"), std::vector<std::string>{"ticks"});
    const atp::runtime::module_node& m = atp_tests::required(proj.group_at("dst")->modules.front().module);
    EXPECT_EQ(m.factory, "counter");
    ASSERT_TRUE(m.factory_version.has_value());
    EXPECT_EQ(*m.factory_version, (atp::version{1, 0}));
    ASSERT_EQ(m.properties.size(), 1u);
    EXPECT_EQ(m.properties.front().first, "step");
    EXPECT_EQ(m.properties.front().second, 5);
}

TEST(StudioClipboard, RepeatedPasteIntoTheSameGroupSuffixesTheName) {
    project proj = project::create();
    proj.add_module("", "counter", "ticks");

    const clipboard clip = proj.copy_children("", {"ticks"});
    (void)proj.paste("", clip, std::nullopt);
    (void)proj.paste("", clip, std::nullopt);

    EXPECT_EQ(children_of(proj, ""), (std::vector<std::string>{"ticks", "ticks_2", "ticks_3"}));
}

TEST(StudioClipboard, PasteRewritesTheInternalConnectionToTheNewNames) {
    project proj = project::create();
    proj.add_module("", "src", "a");
    proj.add_module("", "sink", "b");
    proj.connect("", "a.out", "b.in");

    const clipboard clip = proj.copy_children("", {"a", "b"});
    const std::vector<std::string> made = proj.paste("", clip, std::nullopt);

    ASSERT_EQ(made, (std::vector<std::string>{"a_2", "b_2"}));
    const auto& links = proj.group_at("")->connections;
    ASSERT_EQ(links.size(), 2u);
    EXPECT_EQ(links.back().from, "a_2.out");
    EXPECT_EQ(links.back().to, "b_2.in");
}

TEST(StudioClipboard, PasteOfAGroupBringsItsSubtreeConnectionsAndExports) {
    project proj = project::create();
    proj.add_group("", "box");
    proj.add_module("box", "src", "a");
    proj.add_module("box", "sink", "b");
    proj.connect("box", "a.out", "b.in");
    proj.set_expose_output("box", "out", "a.out");
    proj.add_group("", "dst");

    const clipboard clip = proj.copy_children("", {"box"});
    (void)proj.paste("dst", clip, std::nullopt);

    const atp::runtime::group_node* copy = proj.group_at("dst.box");
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(children_of(proj, "dst.box"), (std::vector<std::string>{"a", "b"}));
    ASSERT_EQ(copy->connections.size(), 1u);
    EXPECT_EQ(copy->connections.front().from, "a.out");
    ASSERT_EQ(copy->expose_outputs.size(), 1u);
    EXPECT_EQ(copy->expose_outputs.front().first, "out");
}

TEST(StudioClipboard, ContentSurvivesTheRemovalOfItsSource) {
    project proj = project::create();
    proj.add_module("", "src", "a");
    proj.add_module("", "sink", "b");
    proj.connect("", "a.out", "b.in");
    proj.add_group("", "dst");

    const clipboard clip = proj.copy_children("", {"a"});
    proj.remove_children("", {"a"});
    EXPECT_TRUE(proj.group_at("")->connections.empty());

    (void)proj.paste("dst", clip, std::nullopt);
    EXPECT_EQ(children_of(proj, "dst"), std::vector<std::string>{"a"});
}

TEST(StudioClipboard, PasteRestoresSubtreePositionsAndAssignmentsUnderTheNewPath) {
    project proj = project::create();
    proj.add_thread("t", atp::thread_mode::on_demand);
    proj.add_group("", "box");
    proj.add_module("box", "src", "a");
    proj.set_position("box", {10.0f, 20.0f});
    proj.set_position("box.a", {30.0f, 40.0f});
    proj.set_assignment("box", "t");
    proj.add_group("", "dst");

    const clipboard clip = proj.copy_children("", {"box"});
    (void)proj.paste("dst", clip, std::nullopt);

    ASSERT_TRUE(proj.position("dst.box").has_value());
    EXPECT_EQ(atp_tests::required(proj.position("dst.box")), (node_position{10.0f, 20.0f}));
    ASSERT_TRUE(proj.position("dst.box.a").has_value());
    EXPECT_EQ(atp_tests::required(proj.position("dst.box.a")), (node_position{30.0f, 40.0f}));

    const auto& assigned = proj.config().assignments;
    const std::pair<std::string, std::string> wanted{"dst.box", "t"};
    EXPECT_NE(std::ranges::find(assigned, wanted), assigned.end());
}

TEST(StudioClipboard, AssignmentToAnUndeclaredThreadIsSkippedOnPaste) {
    project source = project::create();
    source.add_thread("t", atp::thread_mode::on_demand);
    source.add_group("", "box");
    source.set_assignment("box", "t");
    const clipboard clip = source.copy_children("", {"box"});

    project fresh = project::create();
    (void)fresh.paste("", clip, std::nullopt);

    EXPECT_EQ(children_of(fresh, ""), std::vector<std::string>{"box"});
    EXPECT_TRUE(fresh.config().assignments.empty());
}

TEST(StudioClipboard, PasteAtAPositionKeepsTheRelativeGeometry) {
    project proj = project::create();
    proj.add_module("", "src", "a");
    proj.add_module("", "sink", "b");
    proj.set_position("a", {100.0f, 100.0f});
    proj.set_position("b", {160.0f, 140.0f});
    proj.add_group("", "dst");

    const clipboard clip = proj.copy_children("", {"a", "b"});
    const std::vector<std::string> made = proj.paste("dst", clip, node_position{0.0f, 0.0f});

    ASSERT_EQ(made.size(), 2u);
    ASSERT_TRUE(proj.position("dst.a").has_value());
    ASSERT_TRUE(proj.position("dst.b").has_value());
    EXPECT_EQ(atp_tests::required(proj.position("dst.a")), (node_position{0.0f, 0.0f}));
    EXPECT_EQ(atp_tests::required(proj.position("dst.b")), (node_position{60.0f, 40.0f}));
}

TEST(StudioClipboard, PasteIsOneUndoStep) {
    project proj = project::create();
    proj.add_module("", "src", "a");
    proj.add_module("", "sink", "b");
    proj.connect("", "a.out", "b.in");

    const clipboard clip = proj.copy_children("", {"a", "b"});
    (void)proj.paste("", clip, std::nullopt);
    ASSERT_EQ(children_of(proj, "").size(), 4u);

    ASSERT_TRUE(proj.undo());
    EXPECT_EQ(children_of(proj, ""), (std::vector<std::string>{"a", "b"}));
    EXPECT_EQ(proj.group_at("")->connections.size(), 1u);
}

TEST(StudioClipboard, PasteRejectsAnUnknownGroupAndLeavesTheProjectAlone) {
    project proj = project::create();
    proj.add_module("", "src", "a");
    const clipboard clip = proj.copy_children("", {"a"});

    EXPECT_THROW((void)proj.paste("nowhere", clip, std::nullopt), config_error);
    EXPECT_EQ(children_of(proj, ""), std::vector<std::string>{"a"});
}

TEST(StudioClipboard, PastingAnEmptyClipboardIsNotAnOperation) {
    project proj = project::create();
    proj.add_module("", "src", "a");
    const clipboard nothing;
    const bool undoable = proj.can_undo();

    EXPECT_TRUE(proj.paste("", nothing, std::nullopt).empty());

    EXPECT_EQ(proj.can_undo(), undoable);
    EXPECT_EQ(children_of(proj, ""), std::vector<std::string>{"a"});
}

}  // namespace
