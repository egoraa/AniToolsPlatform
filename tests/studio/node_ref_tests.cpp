// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include <atp/studio/node_ref.hpp>

namespace {

using atp::studio::node_ref;

TEST(StudioNodeRef, ParsesRootChild) {
    const node_ref ref = node_ref::parse("stage");
    EXPECT_EQ(ref.group, "");
    EXPECT_EQ(ref.name, "stage");
    EXPECT_FALSE(ref.is_root());
}

TEST(StudioNodeRef, ParsesNestedChild) {
    const node_ref ref = node_ref::parse("a.b.c");
    EXPECT_EQ(ref.group, "a.b");
    EXPECT_EQ(ref.name, "c");
}

TEST(StudioNodeRef, ParsesEmptyPathAsRoot) {
    const node_ref ref = node_ref::parse("");
    EXPECT_TRUE(ref.is_root());
    EXPECT_EQ(ref.full(), "");
}

TEST(StudioNodeRef, JoinsBackToTheParsedPath) {
    for (const char* path : {"", "stage", "a.b", "a.b.c"}) {
        EXPECT_EQ(node_ref::parse(path).full(), path);
    }
}

TEST(StudioNodeRef, GroupWithoutNameAddressesItself) {
    const node_ref entered{"a.b", ""};
    const node_ref selected{"a", "b"};
    EXPECT_EQ(entered.full(), "a.b");
    EXPECT_EQ(selected.full(), "a.b");
    EXPECT_NE(entered, selected);
}

TEST(StudioNodeRef, ContainsItselfAndItsSubtree) {
    const node_ref group{"a", "b"};
    EXPECT_TRUE(group.contains("a.b"));
    EXPECT_TRUE(group.contains("a.b.c"));
    EXPECT_TRUE(group.contains("a.b.c.d"));
}

TEST(StudioNodeRef, DoesNotContainSiblingsOrPrefixNeighbours) {
    const node_ref group{"a", "b"};
    EXPECT_FALSE(group.contains(""));
    EXPECT_FALSE(group.contains("a"));
    EXPECT_FALSE(group.contains("a.c"));
    EXPECT_FALSE(group.contains("a.bb"));
}

TEST(StudioNodeRef, RootContainsEverything) {
    EXPECT_TRUE(node_ref{}.contains(""));
    EXPECT_TRUE(node_ref{}.contains("a.b"));
}

}  // namespace
