#include <filesystem>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include <atp/studio/add_module.hpp>
#include <atp/studio/document.hpp>

namespace {

// No directories are created on disk: relative() resolves a non-existent tail lexically. The root,
// however, is the real temp_directory_path rather than a hand-written "/tmp": on Windows a path
// without a drive name stays drive-relative, and weakly_canonical fills the drive in only for a
// path whose prefix actually exists. Diverging root_names yield an empty lexically_relative, which
// would make the test depend on the contents of the disk.
const std::filesystem::path test_root = std::filesystem::temp_directory_path() / "atp-add-module";
const std::filesystem::path config_dir = test_root / "cfg";

atp::studio::add_module_request request(const char* factory) {
    atp::studio::add_module_request r;
    r.factory = factory;
    r.plugin = config_dir / "libdemo.so";
    r.config_dir = config_dir;
    return r;
}

TEST(StudioAddModule, UsesFactoryNameWhenFree) {
    atp::studio::document doc = atp::studio::document::create();
    const auto result = atp::studio::add_module(doc, request("counter"));
    EXPECT_EQ(result.name, "counter");
    EXPECT_TRUE(result.warning.empty());
    ASSERT_NE(doc.group_at(""), nullptr);
    EXPECT_EQ(doc.group_at("")->modules.size(), 1u);
}

TEST(StudioAddModule, SuffixesNameOnCollision) {
    atp::studio::document doc = atp::studio::document::create();
    EXPECT_EQ(atp::studio::add_module(doc, request("counter")).name, "counter");
    EXPECT_EQ(atp::studio::add_module(doc, request("counter")).name, "counter_2");
    EXPECT_EQ(atp::studio::add_module(doc, request("counter")).name, "counter_3");
}

TEST(StudioAddModule, StoresPositionWhenGiven) {
    atp::studio::document doc = atp::studio::document::create();
    auto r = request("counter");
    r.position = atp::studio::node_position{40.0f, 25.0f};
    const auto result = atp::studio::add_module(doc, r);
    const auto saved = doc.position(result.name);
    ASSERT_TRUE(saved.has_value());
    EXPECT_EQ(*saved, (atp::studio::node_position{40.0f, 25.0f}));
}

TEST(StudioAddModule, LeavesPositionUnsetWithoutRequest) {
    atp::studio::document doc = atp::studio::document::create();
    const auto result = atp::studio::add_module(doc, request("counter"));
    EXPECT_FALSE(doc.position(result.name).has_value());  // the canvas will fall back to auto_layout
}

TEST(StudioAddModule, PluginInsideConfigDirBecomesRelative) {
    atp::studio::document doc = atp::studio::document::create();
    const auto result = atp::studio::add_module(doc, request("counter"));
    ASSERT_EQ(doc.config().plugins.size(), 1u);
    EXPECT_EQ(doc.config().plugins[0], "libdemo.so");
    EXPECT_TRUE(result.warning.empty());
}

TEST(StudioAddModule, PluginOutsideConfigDirWalksUp) {
    atp::studio::document doc = atp::studio::document::create();
    auto r = request("counter");
    r.plugin = test_root / "opt" / "atp" / "libdemo.so";
    const auto result = atp::studio::add_module(doc, r);
    ASSERT_EQ(doc.config().plugins.size(), 1u);
    EXPECT_EQ(doc.config().plugins[0], "../opt/atp/libdemo.so");  // <root>/cfg → <root> → opt
    EXPECT_TRUE(result.warning.empty());  // a relative path came out, so there is nothing to warn about
}

TEST(StudioAddModule, UnsavedDocumentKeepsAbsolutePluginAndWarns) {
    // An empty config_dir means the document has not been saved yet; relative() returns an empty
    // path and the absolute one reaches the config. That is exactly what the warning is about.
    atp::studio::document doc = atp::studio::document::create();
    auto r = request("counter");
    r.config_dir.clear();
    const auto result = atp::studio::add_module(doc, r);
    ASSERT_EQ(doc.config().plugins.size(), 1u);
    EXPECT_EQ(doc.config().plugins[0], (config_dir / "libdemo.so").generic_string());
    EXPECT_FALSE(result.warning.empty());
}

TEST(StudioAddModule, SamePluginListedOnce) {
    atp::studio::document doc = atp::studio::document::create();
    atp::studio::add_module(doc, request("counter"));
    atp::studio::add_module(doc, request("printer"));
    EXPECT_EQ(doc.config().plugins.size(), 1u);
}

}  // namespace
