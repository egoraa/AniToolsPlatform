#include <string>

#include <gtest/gtest.h>

#include <atp/studio/add_group.hpp>
#include <atp/studio/document.hpp>

namespace {

TEST(StudioAddGroup, UsesBaseNameWhenFree) {
    atp::studio::document doc = atp::studio::document::create();
    EXPECT_EQ(atp::studio::add_group(doc, ""), "group");
    ASSERT_NE(doc.group_at("group"), nullptr);
    EXPECT_TRUE(doc.group_at("group")->modules.empty());
}

TEST(StudioAddGroup, SuffixesNameOnCollision) {
    atp::studio::document doc = atp::studio::document::create();
    EXPECT_EQ(atp::studio::add_group(doc, ""), "group");
    EXPECT_EQ(atp::studio::add_group(doc, ""), "group_2");
    EXPECT_EQ(atp::studio::add_group(doc, ""), "group_3");
}

TEST(StudioAddGroup, CollidesWithModuleName) {
    // Modules and groups share one name space, so a module called "group" pushes the suffix too.
    atp::studio::document doc = atp::studio::document::create();
    doc.add_module("", "group");
    EXPECT_EQ(atp::studio::add_group(doc, ""), "group_2");
}

TEST(StudioAddGroup, StoresPositionUnderFullPath) {
    atp::studio::document doc = atp::studio::document::create();
    doc.add_group("", "stage");
    const std::string name = atp::studio::add_group(doc, "stage", atp::studio::node_position{12.0f, 34.0f});
    EXPECT_EQ(name, "group");
    const auto saved = doc.position("stage.group");
    ASSERT_TRUE(saved.has_value());
    EXPECT_EQ(*saved, (atp::studio::node_position{12.0f, 34.0f}));
    EXPECT_FALSE(doc.position("group").has_value());  // the short name is not a node path
}

TEST(StudioAddGroup, LeavesPositionUnsetWithoutRequest) {
    atp::studio::document doc = atp::studio::document::create();
    const std::string name = atp::studio::add_group(doc, "");
    EXPECT_FALSE(doc.position(name).has_value());  // the canvas will fall back to auto_layout
}

TEST(StudioAddGroup, UnknownGroupPathThrows) {
    atp::studio::document doc = atp::studio::document::create();
    EXPECT_THROW((void)atp::studio::add_group(doc, "nowhere"), atp::runtime::config_error);
    EXPECT_TRUE(doc.group_at("")->modules.empty());
    EXPECT_FALSE(doc.can_undo());  // a rejected operation must not grow the history
}

TEST(StudioAddGroup, UndoRemovesTheGroup) {
    atp::studio::document doc = atp::studio::document::create();
    atp::studio::add_group(doc, "");
    ASSERT_TRUE(doc.can_undo());
    EXPECT_TRUE(doc.undo());
    EXPECT_TRUE(doc.group_at("")->modules.empty());
}

}  // namespace
