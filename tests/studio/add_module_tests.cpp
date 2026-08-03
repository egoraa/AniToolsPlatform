#include <filesystem>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include <atp/studio/add_module.hpp>
#include <atp/studio/project.hpp>

namespace {

std::filesystem::path test_root() {
    return std::filesystem::temp_directory_path() / "atp-add-module";
}

std::filesystem::path config_dir() {
    return test_root() / "cfg";
}

atp::studio::add_module_request request(const char* factory) {
    atp::studio::add_module_request r;
    r.factory = factory;
    r.plugin = config_dir() / "libdemo.so";
    r.config_dir = config_dir();
    return r;
}

TEST(StudioAddModule, UsesFactoryNameWhenFree) {
    atp::studio::project proj = atp::studio::project::create();
    const auto result = atp::studio::add_module(proj, request("counter"));
    EXPECT_EQ(result.name, "counter");
    EXPECT_TRUE(result.warning.empty());
    ASSERT_NE(proj.group_at(""), nullptr);
    EXPECT_EQ(proj.group_at("")->modules.size(), 1u);
}

TEST(StudioAddModule, SuffixesNameOnCollision) {
    atp::studio::project proj = atp::studio::project::create();
    EXPECT_EQ(atp::studio::add_module(proj, request("counter")).name, "counter");
    EXPECT_EQ(atp::studio::add_module(proj, request("counter")).name, "counter_2");
    EXPECT_EQ(atp::studio::add_module(proj, request("counter")).name, "counter_3");
}

TEST(StudioAddModule, StoresPositionWhenGiven) {
    atp::studio::project proj = atp::studio::project::create();
    auto r = request("counter");
    r.position = atp::studio::node_position{40.0f, 25.0f};
    const auto result = atp::studio::add_module(proj, r);
    const auto saved = proj.position(result.name);
    ASSERT_TRUE(saved.has_value());
    EXPECT_EQ(*saved, (atp::studio::node_position{40.0f, 25.0f}));
}

TEST(StudioAddModule, LeavesPositionUnsetWithoutRequest) {
    atp::studio::project proj = atp::studio::project::create();
    const auto result = atp::studio::add_module(proj, request("counter"));
    EXPECT_FALSE(proj.position(result.name).has_value());
}

TEST(StudioAddModule, PluginInsideConfigDirBecomesRelative) {
    atp::studio::project proj = atp::studio::project::create();
    const auto result = atp::studio::add_module(proj, request("counter"));
    ASSERT_EQ(proj.config().plugins.size(), 1u);
    EXPECT_EQ(proj.config().plugins[0], "libdemo.so");
    EXPECT_TRUE(result.warning.empty());
}

TEST(StudioAddModule, PluginOutsideConfigDirWalksUp) {
    atp::studio::project proj = atp::studio::project::create();
    auto r = request("counter");
    r.plugin = test_root() / "opt" / "atp" / "libdemo.so";
    const auto result = atp::studio::add_module(proj, r);
    ASSERT_EQ(proj.config().plugins.size(), 1u);
    EXPECT_EQ(proj.config().plugins[0], "../opt/atp/libdemo.so");
    EXPECT_TRUE(result.warning.empty());
}

TEST(StudioAddModule, UnsavedProjectKeepsAbsolutePluginAndWarns) {
    atp::studio::project proj = atp::studio::project::create();
    auto r = request("counter");
    r.config_dir.clear();
    const auto result = atp::studio::add_module(proj, r);
    ASSERT_EQ(proj.config().plugins.size(), 1u);
    EXPECT_EQ(proj.config().plugins[0], (config_dir() / "libdemo.so").generic_string());
    EXPECT_FALSE(result.warning.empty());
}

TEST(StudioAddModule, SamePluginListedOnce) {
    atp::studio::project proj = atp::studio::project::create();
    atp::studio::add_module(proj, request("counter"));
    atp::studio::add_module(proj, request("printer"));
    EXPECT_EQ(proj.config().plugins.size(), 1u);
}

}  // namespace
