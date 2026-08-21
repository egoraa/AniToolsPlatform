// SPDX-License-Identifier: Apache-2.0
#include <chrono>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <atp/runtime/config_model.hpp>
#include <atp/studio/thread_resolve.hpp>

namespace {

atp::runtime::config make_config() {
    atp::runtime::config cfg;
    cfg.threads.push_back({"producing", atp::runtime::thread_mode::throttled, std::chrono::milliseconds{500}});
    cfg.threads.push_back({"consuming", atp::runtime::thread_mode::on_demand, {}});
    return cfg;
}

TEST(ThreadResolve, AssignedGroupOwnsItsThread) {
    atp::runtime::config cfg = make_config();
    cfg.assignments.emplace_back("workers", "consuming");

    const atp::studio::resolved_thread r = atp::studio::resolve_thread(cfg, "workers");
    EXPECT_EQ(r.name, "consuming");
    EXPECT_FALSE(r.inherited);
}

TEST(ThreadResolve, NestedGroupInheritsFromNearestAssignedAncestor) {
    atp::runtime::config cfg = make_config();
    cfg.assignments.emplace_back("outer", "consuming");

    const atp::studio::resolved_thread r = atp::studio::resolve_thread(cfg, "outer.middle.inner");
    EXPECT_EQ(r.name, "consuming");
    EXPECT_TRUE(r.inherited);
}

TEST(ThreadResolve, NearestAncestorWinsOverFartherOne) {
    atp::runtime::config cfg = make_config();
    cfg.assignments.emplace_back("outer", "consuming");
    cfg.assignments.emplace_back("outer.middle", "producing");

    const atp::studio::resolved_thread r = atp::studio::resolve_thread(cfg, "outer.middle.inner");
    EXPECT_EQ(r.name, "producing");
    EXPECT_TRUE(r.inherited);
}

TEST(ThreadResolve, UnassignedFallsBackToFirstDeclaredThread) {
    const atp::runtime::config cfg = make_config();

    const atp::studio::resolved_thread root = atp::studio::resolve_thread(cfg, "");
    EXPECT_EQ(root.name, "producing");
    EXPECT_TRUE(root.inherited);

    const atp::studio::resolved_thread orphan = atp::studio::resolve_thread(cfg, "workers");
    EXPECT_EQ(orphan.name, "producing");
    EXPECT_TRUE(orphan.inherited);
}

TEST(ThreadResolve, WithoutDeclaredThreadsEverythingRunsOnImplicitMain) {
    const atp::runtime::config cfg;

    const atp::studio::resolved_thread r = atp::studio::resolve_thread(cfg, "workers");
    EXPECT_EQ(r.name, "main");
    EXPECT_TRUE(r.inherited);
}

TEST(ThreadResolve, PrefixMatchRespectsPathSeparator) {
    atp::runtime::config cfg = make_config();
    cfg.assignments.emplace_back("outer", "consuming");

    const atp::studio::resolved_thread r = atp::studio::resolve_thread(cfg, "outer2");
    EXPECT_EQ(r.name, "producing");
    EXPECT_TRUE(r.inherited);
}

TEST(ThreadResolve, GroupsOnThreadKeepsAssignmentOrder) {
    atp::runtime::config cfg = make_config();
    cfg.assignments.emplace_back("b", "consuming");
    cfg.assignments.emplace_back("a", "producing");
    cfg.assignments.emplace_back("c", "consuming");

    EXPECT_EQ(atp::studio::groups_on_thread(cfg, "consuming"), (std::vector<std::string>{"b", "c"}));
    EXPECT_EQ(atp::studio::groups_on_thread(cfg, "producing"), (std::vector<std::string>{"a"}));
    EXPECT_TRUE(atp::studio::groups_on_thread(cfg, "missing").empty());
}

}  // namespace
