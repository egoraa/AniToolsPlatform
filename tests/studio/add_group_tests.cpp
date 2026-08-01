#include <string>

#include <gtest/gtest.h>

#include <atp/studio/add_group.hpp>
#include <atp/studio/project.hpp>

namespace {

TEST(StudioAddGroup, UsesBaseNameWhenFree) {
    atp::studio::project proj = atp::studio::project::create();
    EXPECT_EQ(atp::studio::add_group(proj, ""), "group");
    ASSERT_NE(proj.group_at("group"), nullptr);
    EXPECT_TRUE(proj.group_at("group")->modules.empty());
}

TEST(StudioAddGroup, SuffixesNameOnCollision) {
    atp::studio::project proj = atp::studio::project::create();
    EXPECT_EQ(atp::studio::add_group(proj, ""), "group");
    EXPECT_EQ(atp::studio::add_group(proj, ""), "group_2");
    EXPECT_EQ(atp::studio::add_group(proj, ""), "group_3");
}

TEST(StudioAddGroup, CollidesWithModuleName) {
    atp::studio::project proj = atp::studio::project::create();
    proj.add_module("", "group");
    EXPECT_EQ(atp::studio::add_group(proj, ""), "group_2");
}

TEST(StudioAddGroup, StoresPositionUnderFullPath) {
    atp::studio::project proj = atp::studio::project::create();
    proj.add_group("", "stage");
    const std::string name = atp::studio::add_group(proj, "stage", atp::studio::node_position{12.0f, 34.0f});
    EXPECT_EQ(name, "group");
    const auto saved = proj.position("stage.group");
    ASSERT_TRUE(saved.has_value());
    EXPECT_EQ(*saved, (atp::studio::node_position{12.0f, 34.0f}));
    EXPECT_FALSE(proj.position("group").has_value());
}

TEST(StudioAddGroup, LeavesPositionUnsetWithoutRequest) {
    atp::studio::project proj = atp::studio::project::create();
    const std::string name = atp::studio::add_group(proj, "");
    EXPECT_FALSE(proj.position(name).has_value());
}

TEST(StudioAddGroup, UnknownGroupPathThrows) {
    atp::studio::project proj = atp::studio::project::create();
    EXPECT_THROW((void)atp::studio::add_group(proj, "nowhere"), atp::runtime::config_error);
    EXPECT_TRUE(proj.group_at("")->modules.empty());
    EXPECT_FALSE(proj.can_undo());
}

TEST(StudioAddGroup, UndoRemovesTheGroup) {
    atp::studio::project proj = atp::studio::project::create();
    atp::studio::add_group(proj, "");
    ASSERT_TRUE(proj.can_undo());
    EXPECT_TRUE(proj.undo());
    EXPECT_TRUE(proj.group_at("")->modules.empty());
}

}  // namespace
