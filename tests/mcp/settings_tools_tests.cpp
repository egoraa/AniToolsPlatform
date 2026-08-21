// SPDX-License-Identifier: Apache-2.0
#include <filesystem>
#include <memory>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atp/mcp/document_tools.hpp>
#include <atp/mcp/settings_tools.hpp>
#include <atp/mcp/tool_registry.hpp>
#include <atp/mcp/workspace.hpp>
#include <atp/module.hpp>
#include <atp/runtime/config_model.hpp>

namespace {

struct settings_props : atp::io::properties {
    atp::io::property<int>& limit = make<atp::io::property<int>>("limit", 10);
};
using settings_ports = atp::ports<atp::io::inputs, atp::io::outputs, settings_props>;
class settings_module : public atp::module<settings_ports, "settings_demo"> {};

class McpSettingsTools : public ::testing::Test {
   protected:
    void SetUp() override {
        root_ = std::filesystem::temp_directory_path() / "atp_mcp_settings";
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_);
        ws_ = std::make_unique<atp::mcp::workspace>(root_);
        ws_->modules().registry().add<settings_module>();
        atp::mcp::register_document_tools(tools_, *ws_);
        atp::mcp::register_settings_tools(tools_, *ws_);
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

TEST_F(McpSettingsTools, WritesAndClearsAProperty) {
    call("add_module", {{"group_path", ""}, {"factory", "settings_demo"}, {"name", "m"}});
    call("set_property", {{"group_path", ""}, {"name", "m"}, {"property", "limit"}, {"value", 5}});
    const nlohmann::json document = call("get_document").at("document");
    EXPECT_EQ(document.at("pipeline").at("modules").at(0).at("properties").at("limit"), 5);

    call("clear_property", {{"group_path", ""}, {"name", "m"}, {"property", "limit"}});
    EXPECT_FALSE(call("get_document").at("document").at("pipeline").at("modules").at(0).contains("properties"));
}

TEST_F(McpSettingsTools, RefusesANonScalarPropertyValue) {
    call("add_module", {{"group_path", ""}, {"factory", "settings_demo"}, {"name", "m"}});
    EXPECT_THROW(
        (void)call(
            "set_property",
            {{"group_path", ""}, {"name", "m"}, {"property", "limit"}, {"value", nlohmann::json{{"nested", 1}}}}),
        atp::runtime::config_error);
}

TEST_F(McpSettingsTools, DeclaresThreadsAndAssignsGroupsToThem) {
    call("add_group", {{"group_path", ""}, {"name", "stage"}});
    call("add_thread", {{"name", "worker"}, {"mode", "throttled"}, {"period_ms", 10}});
    call("set_assignment", {{"group_path", "stage"}, {"thread", "worker"}});
    const nlohmann::json document = call("get_document").at("document");
    EXPECT_EQ(document.at("threads").at(0).at("name"), "worker");
    EXPECT_EQ(document.at("threads").at(0).at("mode"), "throttled");
    EXPECT_EQ(document.at("threads").at(0).at("period_ms"), 10);

    call("clear_assignment", {{"group_path", "stage"}});
    call("remove_thread", {{"name", "worker"}});
    EXPECT_FALSE(call("get_document").at("document").contains("threads"));
}

TEST_F(McpSettingsTools, RefusesAThrottledThreadWithoutAPeriodAndAnUnknownMode) {
    EXPECT_THROW((void)call("add_thread", {{"name", "t"}, {"mode", "throttled"}}), atp::runtime::config_error);
    EXPECT_THROW((void)call("add_thread", {{"name", "t"}, {"mode", "turbo"}}), atp::runtime::config_error);
}

}  // namespace
