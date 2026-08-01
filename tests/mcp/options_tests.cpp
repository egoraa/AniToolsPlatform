#include <algorithm>
#include <filesystem>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include <atp/mcp/options.hpp>
#include <atp/mcp/workspace.hpp>

namespace {

std::optional<atp::mcp::options> parse(std::vector<const char*> argv) {
    return atp::mcp::parse_options(static_cast<int>(argv.size()), argv.data());
}

TEST(McpOptions, DefaultsToTheCurrentDirectoryAndNoDirectories) {
    const std::optional<atp::mcp::options> parsed = parse({"atp_mcp"});
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->root, std::filesystem::current_path());
    EXPECT_TRUE(parsed->plugin_dirs.empty());
    EXPECT_TRUE(parsed->scan_dirs.empty());
}

TEST(McpOptions, ReadsTheRootAndBothRepeatableDirectoryFlags) {
    const std::optional<atp::mcp::options> parsed =
        parse({"atp_mcp", "--root", "/work", "--plugin-dir", "/trusted", "--scan-dir", "/first", "--scan-dir",
               "/second", "--plugin-dir", "/also_trusted"});
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->root, std::filesystem::path("/work"));
    ASSERT_EQ(parsed->plugin_dirs.size(), 2u);
    EXPECT_EQ(parsed->plugin_dirs.at(0), std::filesystem::path("/trusted"));
    EXPECT_EQ(parsed->plugin_dirs.at(1), std::filesystem::path("/also_trusted"));
    ASSERT_EQ(parsed->scan_dirs.size(), 2u);
    EXPECT_EQ(parsed->scan_dirs.at(0), std::filesystem::path("/first"));
    EXPECT_EQ(parsed->scan_dirs.at(1), std::filesystem::path("/second"));
}

TEST(McpOptions, RejectsUnknownFlagsAndFlagsWithoutAValue) {
    EXPECT_FALSE(parse({"atp_mcp", "--scan"}).has_value());
    EXPECT_FALSE(parse({"atp_mcp", "--scan-dir"}).has_value());
    EXPECT_FALSE(parse({"atp_mcp", "--root"}).has_value());
    EXPECT_FALSE(parse({"atp_mcp", "/bare/positional"}).has_value());
}

TEST(McpWorkspaceScanDirs, FillsTheCatalogAtConstruction) {
    const std::filesystem::path plugin_dir = std::filesystem::path(ATP_TEST_PLUGIN).parent_path();
    atp::mcp::workspace ws(std::filesystem::temp_directory_path(), {}, {plugin_dir});

    const std::vector<atp::studio::plugin_info>& plugins = ws.modules().plugins();
    const auto loaded = std::ranges::find_if(plugins, [](const atp::studio::plugin_info& info) {
        return info.path == std::filesystem::weakly_canonical(std::filesystem::path(ATP_TEST_PLUGIN));
    });
    ASSERT_NE(loaded, plugins.end());
    EXPECT_TRUE(loaded->loaded);
    EXPECT_FALSE(loaded->modules.empty());
}

TEST(McpWorkspaceScanDirs, TrustsAScannedDirectoryForExplicitLoadsToo) {
    const std::filesystem::path plugin_dir = std::filesystem::path(ATP_TEST_PLUGIN).parent_path();
    atp::mcp::workspace ws(std::filesystem::temp_directory_path(), {}, {plugin_dir});

    EXPECT_EQ(ws.resolve_plugin(ATP_TEST_PLUGIN), std::filesystem::weakly_canonical(ATP_TEST_PLUGIN));
}

TEST(McpWorkspaceScanDirs, LeavesTheCatalogEmptyWithoutAnyScanDirectory) {
    atp::mcp::workspace ws(std::filesystem::temp_directory_path());
    EXPECT_TRUE(ws.modules().plugins().empty());
}

}  // namespace
