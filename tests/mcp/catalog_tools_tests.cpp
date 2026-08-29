// SPDX-License-Identifier: Apache-2.0
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atp/plugin_c.h>
#include <atp/io/enum_names.hpp>
#include <atp/mcp/catalog_tools.hpp>
#include <atp/mcp/tool_registry.hpp>
#include <atp/mcp/workspace.hpp>
#include <atp/module.hpp>
#include <atp/runtime/c_module.hpp>
#include <atp/runtime/config_model.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>

namespace {

enum class channel_layout { mono, stereo, surround };

}  // namespace

template <>
struct atp::io::enum_names<channel_layout> {
    static constexpr std::array entries{
        atp::io::enum_entry{channel_layout::mono, "mono"},
        atp::io::enum_entry{channel_layout::stereo, "stereo"},
        atp::io::enum_entry{channel_layout::surround, "surround"},
    };
};

namespace {

struct catalog_props : atp::io::properties {
    atp::io::property<int>& limit = make<atp::io::property<int>>("limit", 10);
};
struct catalog_outputs : atp::io::outputs {
    atp::io::output<int>& value = make<atp::io::output<int>>("value");
};
using catalog_ports = atp::ports<atp::io::inputs, catalog_outputs, catalog_props>;
class catalog_module : public atp::module<catalog_ports, "catalog_demo"> {};

struct catalog_config : atp::module_config {
    using module_config::module_config;
    std::int64_t& size = field("size", std::int64_t{16});
    std::string& device = field<std::string>("device");
};

class catalog_configured : public atp::module<atp::ports<>, "catalog_configured"> {
   public:
    using config_type = catalog_config;
    explicit catalog_configured(std::unique_ptr<catalog_config> cfg) : config_(std::move(cfg)) {}

   private:
    std::unique_ptr<catalog_config> config_;
};

struct catalog_enum_config : atp::module_config {
    using module_config::module_config;
    channel_layout& layout = field("layout", channel_layout::stereo);
    std::deque<channel_layout>& busses = list<channel_layout>("busses");
};

class catalog_enum : public atp::module<atp::ports<>, "catalog_enum"> {
   public:
    using config_type = catalog_enum_config;
    explicit catalog_enum(std::unique_ptr<catalog_enum_config> cfg) : config_(std::move(cfg)) {}

   private:
    std::unique_ptr<catalog_enum_config> config_;
};

class catalog_opaque : public atp::module<atp::ports<>, "catalog_opaque"> {
   public:
    using config_type = atp::module_config;
    explicit catalog_opaque(std::unique_ptr<atp::module_config> cfg) : config_(std::move(cfg)) {}

   private:
    std::unique_ptr<atp::module_config> config_;
};

void* c_declared_create(const atp_api*, atp_ctx*, void*) {
    return nullptr;
}

void c_declared_destroy(void*) {}

atp_work c_declared_iterate(void*) {
    return ATP_WORK_IDLE;
}

const char* const c_declared_engines[] = {"fm", "additive"};

const atp_config_field_desc c_declared_fields[] = {
    {"rate", ATP_FIELD_INT, nullptr, nullptr, 0, ATP_FIELD_STRING, nullptr, 0},
    {"engine", ATP_FIELD_STRING, "fm", c_declared_engines, 2, ATP_FIELD_STRING, nullptr, 0},
};

const atp_module_desc& c_declared_desc() {
    static const atp_module_desc desc = [] {
        atp_module_desc d{};
        d.struct_size = sizeof(atp_module_desc);
        d.name = "catalog_c_declared";
        d.version[0] = 1;
        d.version_count = 1;
        d.create = c_declared_create;
        d.destroy = c_declared_destroy;
        d.iterate = c_declared_iterate;
        d.config_fields = c_declared_fields;
        d.config_field_count = 2;
        return d;
    }();
    return desc;
}

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
        ws_->modules().registry().add(std::make_unique<atp::runtime::c_module_factory>(c_declared_desc()));
        ws_->modules().registry().add<catalog_opaque>();
        ws_->modules().registry().add<catalog_enum>();
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
    EXPECT_EQ(module.at("properties").at(0).at("schema").at("type"), "integer");
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
        << "the key is there exactly when a config reaches the module, and none reaches this one";
}

TEST_F(McpCatalogTools, AModuleTakingAConfigItDoesNotDescribeCarriesTheKeyWithNoFields) {
    const nlohmann::json modules = call("list_modules").at("modules");
    const auto found =
        std::ranges::find_if(modules, [](const nlohmann::json& m) { return m.at("name") == "catalog_opaque"; });
    ASSERT_NE(found, modules.end());

    ASSERT_TRUE(found->contains("config"))
        << "this module reads what it is given through text(), which is what a \"file:\" config is for; "
           "saying it takes none is how such a config stops being written: "
        << found->dump();
    EXPECT_TRUE(found->at("config").at("fields").empty()) << "and it declares no field of it";
}

TEST_F(McpCatalogTools, AnEnumFieldCarriesItsNamesAsAnEnumKeyword) {
    const nlohmann::json modules = call("list_modules").at("modules");
    const auto found =
        std::ranges::find_if(modules, [](const nlohmann::json& m) { return m.at("name") == "catalog_enum"; });
    ASSERT_NE(found, modules.end());

    const nlohmann::json& fields = found->at("config").at("fields");
    ASSERT_EQ(fields.size(), 2u);
    EXPECT_EQ(fields.at(0).at("type"), "string") << "in a document an enumeration is a name";
    EXPECT_EQ(fields.at(0).at("default"), "stereo");
    EXPECT_EQ(fields.at(0).at("enum"), nlohmann::json::array({"mono", "stereo", "surround"}))
        << "the same keyword a property schema uses: one idea, one vocabulary";
    EXPECT_EQ(fields.at(1).at("items").at("enum"), nlohmann::json::array({"mono", "stereo", "surround"}))
        << "on an array it is each element that is constrained, not the array";
}

TEST_F(McpCatalogTools, AModuleOfTheCPathThatDeclaresItsConfigSaysSoTheSameWay) {
    const nlohmann::json modules = call("list_modules").at("modules");
    const auto found =
        std::ranges::find_if(modules, [](const nlohmann::json& m) { return m.at("name") == "catalog_c_declared"; });
    ASSERT_NE(found, modules.end());

    ASSERT_TRUE(found->contains("config")) << found->dump();
    const nlohmann::json& fields = found->at("config").at("fields");
    ASSERT_EQ(fields.size(), 2u);
    EXPECT_EQ(fields.at(0).at("name"), "rate");
    EXPECT_EQ(fields.at(0).at("required"), true);
    EXPECT_EQ(fields.at(1).at("name"), "engine");
    EXPECT_EQ(fields.at(1).at("default"), "fm");
    EXPECT_EQ(fields.at(1).at("enum"), (nlohmann::json{"fm", "additive"}))
        << "a declaration that crossed the C boundary is the same declaration here";
}
