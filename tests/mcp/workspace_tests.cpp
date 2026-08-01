#include <filesystem>
#include <vector>

#include <gtest/gtest.h>

#include <atp/mcp/workspace.hpp>
#include <atp/runtime/config_model.hpp>

namespace {

std::filesystem::path make_root(const char* name) {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return std::filesystem::weakly_canonical(root);
}

TEST(McpWorkspace, StartsWithAnEmptyDocumentAtTheCurrentSchema) {
    const std::filesystem::path root = make_root("atp_mcp_ws_empty");
    atp::mcp::workspace ws(root);
    EXPECT_EQ(ws.project().config().schema, atp::runtime::config_schema_version);
    EXPECT_FALSE(ws.project_path().has_value());
    EXPECT_FALSE(ws.run_session().running());
    std::filesystem::remove_all(root);
}

TEST(McpWorkspace, ResolvesRelativePathsAgainstTheRoot) {
    const std::filesystem::path root = make_root("atp_mcp_ws_resolve");
    atp::mcp::workspace ws(root);
    EXPECT_EQ(ws.resolve("a/b.json"), std::filesystem::weakly_canonical(root / "a/b.json"));
    EXPECT_EQ(ws.resolve("./x.json"), std::filesystem::weakly_canonical(root / "x.json"));
    std::filesystem::remove_all(root);
}

TEST(McpWorkspace, RefusesPathsThatEscapeTheRoot) {
    const std::filesystem::path root = make_root("atp_mcp_ws_escape");
    atp::mcp::workspace ws(root);
    EXPECT_THROW((void)ws.resolve("../outside.json"), atp::runtime::config_error);
    EXPECT_THROW((void)ws.resolve("a/../../outside.json"), atp::runtime::config_error);
    const std::filesystem::path absolute_outside = std::filesystem::temp_directory_path() / "elsewhere.json";
    EXPECT_THROW((void)ws.resolve(absolute_outside.string()), atp::runtime::config_error);
    std::filesystem::remove_all(root);
}

TEST(McpWorkspace, AllowsPluginsFromTheExplicitlyListedDirectoriesOnly) {
    const std::filesystem::path root = make_root("atp_mcp_ws_plugins");
    const std::filesystem::path allowed = make_root("atp_mcp_ws_plugin_dir");
    atp::mcp::workspace ws(root, std::vector<std::filesystem::path>{allowed});

    const std::filesystem::path inside_allowed = allowed / "demo.dll";
    EXPECT_EQ(ws.resolve_plugin(inside_allowed.string()), std::filesystem::weakly_canonical(inside_allowed));
    EXPECT_EQ(ws.resolve_plugin("local.dll"), std::filesystem::weakly_canonical(root / "local.dll"));

    const std::filesystem::path elsewhere = std::filesystem::temp_directory_path() / "atp_mcp_ws_forbidden.dll";
    EXPECT_THROW((void)ws.resolve_plugin(elsewhere.string()), atp::runtime::config_error);
    EXPECT_THROW((void)ws.resolve(inside_allowed.string()), atp::runtime::config_error);

    std::filesystem::remove_all(allowed);
    std::filesystem::remove_all(root);
}

TEST(McpWorkspace, SavesOpensAndRemembersTheDocumentPath) {
    const std::filesystem::path root = make_root("atp_mcp_ws_roundtrip");
    atp::mcp::workspace ws(root);
    ws.project().add_group("", "stage");

    const std::filesystem::path file = ws.resolve("pipeline.json");
    ws.save_document(file);
    ASSERT_TRUE(std::filesystem::exists(file));
    ASSERT_TRUE(ws.project_path().has_value());
    EXPECT_EQ(*ws.project_path(), file);
    EXPECT_EQ(ws.project_dir(), root);

    atp::mcp::workspace reopened(root);
    reopened.open_document(file);
    ASSERT_NE(reopened.project().group_at("stage"), nullptr);

    reopened.reset_document();
    EXPECT_EQ(reopened.project().group_at("stage"), nullptr);
    EXPECT_FALSE(reopened.project_path().has_value());
    std::filesystem::remove_all(root);
}

}  // namespace
