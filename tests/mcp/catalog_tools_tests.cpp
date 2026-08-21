// SPDX-License-Identifier: Apache-2.0
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atp/mcp/catalog_tools.hpp>
#include <atp/mcp/tool_registry.hpp>
#include <atp/mcp/workspace.hpp>
#include <atp/module.hpp>
#include <atp/runtime/config_model.hpp>

#include <algorithm>
#include <cstdint>

namespace {

struct catalog_props : atp::io::properties {
    atp::io::property<int>& limit = make<atp::io::property<int>>("limit", 10);
};
struct catalog_outputs : atp::io::outputs {
    atp::io::output<int>& value = make<atp::io::output<int>>("value");
};
using catalog_ports = atp::ports<atp::io::inputs, catalog_outputs, catalog_props>;
class catalog_module : public atp::module<catalog_ports, "catalog_demo"> {};

struct catalog_config : atp::config::fields {
    using fields::fields;
    std::int64_t& size = field("size", std::int64_t{16});
    std::string& device = field<std::string>("device");
};

class catalog_configured : public atp::module<atp::ports<>, "catalog_configured"> {
   public:
    using config_type = catalog_config;
    explicit catalog_configured(const atp::module_config& cfg) : config_(cfg) {}

   private:
    catalog_config config_;
};

class McpCatalogTools : public ::testing::Test {
   protected:
    void SetUp() override {
        root_ = std::filesystem::temp_directory_path() / "atp_mcp_catalog";
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_);
        ws_ = std::make_unique<atp::mcp::workspace>(
            root_, std::vector<std::filesystem::path>{std::filesystem::path(ATP_TEST_PLUGIN).parent_path()});
        ws_->modules().registry().add<catalog_module>();
        ws_->modules().registry().add<catalog_configured>();
        atp::mcp::register_catalog_tools(tools_, *ws_);
    }

    void TearDown() override {
        ws_.reset();
        std::filesystem::remove_all(root_);
    }

    nlohmann::json call(const char* name, const nlohmann::json& args = nlohmann::json::object()) {
        const atp::mcp::tool* t = tools_.find(name);
        EXPECT_NE(t, nullptr) << name;
        return t == nullptr ? nlohmann::json::object() : t->run(args);
    }

    std::filesystem::path root_;
    std::unique_ptr<atp::mcp::workspace> ws_;
    atp::mcp::tool_registry tools_;
};

TEST_F(McpCatalogTools, ListsARegisteredModuleWithItsPortsAndPropertySchema) {
    const nlohmann::json modules = call("list_modules").at("modules");
    ASSERT_FALSE(modules.empty());
    const auto found =
        std::ranges::find_if(modules, [](const nlohmann::json& m) { return m.at("name") == "catalog_demo"; });
    ASSERT_NE(found, modules.end());
    const nlohmann::json& module = *found;
    EXPECT_EQ(module.at("name"), "catalog_demo");
    EXPECT_EQ(module.at("broken"), false);
    EXPECT_EQ(module.at("outputs").at(0).at("name"), "value");
    EXPECT_EQ(module.at("properties").at(0).at("name"), "limit");
    EXPECT_EQ(module.at("properties").at(0).at("schema").at("type"), "number");
}

TEST_F(McpCatalogTools, LoadsTheTestPluginAndReportsIt) {
    const nlohmann::json loaded = call("load_plugin", nlohmann::json{{"path", ATP_TEST_PLUGIN}});
    EXPECT_EQ(loaded.at("loaded"), true);
    const nlohmann::json plugins = call("list_plugins").at("plugins");
    ASSERT_EQ(plugins.size(), 1u);
    EXPECT_EQ(plugins.at(0).at("loaded"), true);
}

TEST_F(McpCatalogTools, ReportsABadPluginWithoutThrowing) {
    const nlohmann::json loaded = call("load_plugin", nlohmann::json{{"path", ATP_TEST_PLUGIN_BAD_ABI}});
    EXPECT_EQ(loaded.at("loaded"), false);
    EXPECT_FALSE(loaded.at("error").get<std::string>().empty());
}

TEST_F(McpCatalogTools, RefusesASearchDirectoryOutsideTheAllowedDirectories) {
    EXPECT_THROW((void)call("add_plugin_search_dir", nlohmann::json{{"path", "../elsewhere"}}),
                 atp::runtime::config_error);
}

TEST_F(McpCatalogTools, RefusesAPluginOutsideTheAllowedDirectories) {
    const std::filesystem::path elsewhere = std::filesystem::temp_directory_path() / "atp_mcp_forbidden.dll";
    EXPECT_THROW((void)call("load_plugin", nlohmann::json{{"path", elsewhere.string()}}), atp::runtime::config_error);
}

}  // namespace

TEST_F(McpCatalogTools, AModuleThatDeclaresItsConfigSaysSoInTheCatalog) {
    const nlohmann::json modules = call("list_modules").at("modules");
    const auto found =
        std::ranges::find_if(modules, [](const nlohmann::json& m) { return m.at("name") == "catalog_configured"; });
    ASSERT_NE(found, modules.end());

    ASSERT_TRUE(found->contains("config")) << found->dump();
    const nlohmann::json& fields = found->at("config").at("fields");
    ASSERT_EQ(fields.size(), 2u);
    EXPECT_EQ(fields.at(0).at("name"), "size");
    EXPECT_EQ(fields.at(0).at("type"), "integer");
    EXPECT_EQ(fields.at(0).at("default"), 16);
    EXPECT_FALSE(fields.at(0).contains("required"));
    EXPECT_EQ(fields.at(1).at("name"), "device");
    EXPECT_EQ(fields.at(1).at("required"), true);
    EXPECT_FALSE(fields.at(1).contains("default"))
        << "a required field has no default, and printing null for one would read as a value";
}

TEST_F(McpCatalogTools, AModuleWithoutADeclaredConfigCarriesNoConfigKey) {
    const nlohmann::json modules = call("list_modules").at("modules");
    const auto found =
        std::ranges::find_if(modules, [](const nlohmann::json& m) { return m.at("name") == "catalog_demo"; });
    ASSERT_NE(found, modules.end());

    EXPECT_FALSE(found->contains("config"))
        << "no schema and an empty schema mean different things: the first says edit this as text";
}
