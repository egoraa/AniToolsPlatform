// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeindex>
#include <typeinfo>
#include <vector>

#include <gtest/gtest.h>

#include <atp/plugin_c.h>
#include <atp/hosting/module_factory.hpp>
#include <atp/module.hpp>
#include <atp/runtime/c_module.hpp>
#include <atp/studio/module_manager.hpp>

namespace {

const atp::studio::plugin_info* find_plugin(const atp::studio::module_manager& m, const std::string& stem) {
    for (const atp::studio::plugin_info& p : m.plugins()) {
        if (p.path.stem().string() == stem) {
            return &p;
        }
    }
    return nullptr;
}

TEST(ModuleManager, ScansDirectoryLoadingValidAndReportingFailures) {
    atp::studio::module_manager manager;
    manager.add_search_dir(std::filesystem::path(ATP_TEST_PLUGIN).parent_path());
    manager.rescan();

    const auto* good = find_plugin(manager, std::filesystem::path(ATP_TEST_PLUGIN).stem().string());
    ASSERT_NE(good, nullptr);
    EXPECT_TRUE(good->loaded);
    ASSERT_EQ(good->modules.size(), 2u);
    EXPECT_NE(manager.registry().find("plugin_module"), nullptr);

    const auto* empty = find_plugin(manager, std::filesystem::path(ATP_TEST_PLUGIN_EMPTY).stem().string());
    ASSERT_NE(empty, nullptr);
    EXPECT_FALSE(empty->loaded);
    EXPECT_NE(empty->error.find("atp_abi_version"), std::string::npos);

    const auto* bad = find_plugin(manager, std::filesystem::path(ATP_TEST_PLUGIN_BAD_ABI).stem().string());
    ASSERT_NE(bad, nullptr);
    EXPECT_FALSE(bad->loaded);
    EXPECT_NE(bad->error.find("ABI"), std::string::npos);

    manager.rescan();
    EXPECT_EQ(manager.registry().versions("plugin_module").size(), 1u);
}

TEST(ModuleManager, DuplicateModulesRejectPluginEntirely) {
    atp::studio::module_manager manager;
    manager.load_plugin(ATP_TEST_PLUGIN);

    const auto copy = std::filesystem::temp_directory_path() / "copy_of_test_plugin.dll";
    std::filesystem::copy_file(ATP_TEST_PLUGIN, copy, std::filesystem::copy_options::overwrite_existing);
    manager.load_plugin(copy);

    const auto* dup = find_plugin(manager, "copy_of_test_plugin");
    ASSERT_NE(dup, nullptr);
    EXPECT_FALSE(dup->loaded);
    EXPECT_NE(dup->error.find("duplicate"), std::string::npos);
    EXPECT_EQ(manager.registry().versions("plugin_module").size(), 1u);
}

struct probe_inputs : atp::io::inputs {
    atp::io::input<int>& value = make<atp::io::input<int>>("value");
};
struct probe_outputs : atp::io::outputs {
    atp::io::output<int>& count = make<atp::io::output<int>>("count");
};
using probe_ports = atp::ports<probe_inputs, probe_outputs>;
class probed_module : public atp::module<probe_ports, "probed"> {};

class throwing_module : public atp::module<atp::ports<>, "throwing"> {
   public:
    throwing_module() {
        throw std::runtime_error("broken constructor");
    }
};

TEST(ModuleManager, DescribeListsPortsWithoutConstructingTheModule) {
    atp::module_factory<probed_module> good("probed");
    const atp::studio::module_info info = atp::studio::module_manager::describe(good);
    EXPECT_FALSE(info.broken);
    ASSERT_EQ(info.inputs.size(), 1u);
    EXPECT_EQ(info.inputs[0].name, "value");
    EXPECT_EQ(info.inputs[0].type, std::type_index(typeid(int)));
    ASSERT_EQ(info.outputs.size(), 1u);
    EXPECT_EQ(info.outputs[0].name, "count");
}

TEST(ModuleManager, ABrokenConstructorNoLongerMakesAModuleUndescribable) {
    atp::module_factory<throwing_module> factory("throwing");
    const atp::studio::module_info info = atp::studio::module_manager::describe(factory);

    EXPECT_FALSE(info.broken) << "ports are declared by the port node, so a constructor that throws says nothing about "
                                 "what the module declares";
    EXPECT_TRUE(info.error.empty());
}

struct hungry_config : atp::module_config {
    using module_config::module_config;
    std::int64_t& channels = field<std::int64_t>("channels");
};

class config_hungry_module : public atp::module<atp::ports<>, "hungry"> {
   public:
    using config_type = hungry_config;

    explicit config_hungry_module(std::unique_ptr<hungry_config> config) {
        if (!config->find("channels")->is_set()) {
            throw std::runtime_error("channels is required");
        }
    }
};

TEST(ModuleManager, AModuleWhoseConstructorDemandsAConfigIsStillDescribable) {
    atp::module_factory<config_hungry_module> factory("hungry");
    const atp::studio::module_info info = atp::studio::module_manager::describe(factory);

    EXPECT_FALSE(info.broken) << "tolerating an empty config was the price of probing, and probing is gone";
    EXPECT_TRUE(info.error.empty());
}

}  // namespace

namespace {
enum class fit { none, cover, contain };
}

template <>
struct atp::io::enum_names<fit> {
    static constexpr std::array entries{
        atp::io::enum_entry{fit::none, "none"},
        atp::io::enum_entry{fit::cover, "cover"},
        atp::io::enum_entry{fit::contain, "contain"},
    };
};

namespace {

struct probe_props : atp::io::properties {
    atp::io::property<int>& limit = make<atp::io::property<int>>("limit", 10);
    atp::io::property<std::string>& tmp = make<atp::io::property<std::string>>("tmp", "", atp::io::transient);
    atp::io::property<fit>& scaling = make<atp::io::property<fit>>("scaling", fit::cover);
    atp::io::property<int>& channels = make<atp::io::property<int>>("channels", 2, atp::io::allowed(1, 2, 6));
};
class propertied_probe
    : public atp::module<atp::ports<atp::io::inputs, atp::io::outputs, probe_props>, "propertied_probe"> {};

TEST(StudioModuleManager, DescribeListsPropertyOptions) {
    atp::module_factory<propertied_probe> factory("propertied_probe");
    const atp::studio::module_info info = atp::studio::module_manager::describe(factory);
    const auto by_name = [&info](std::string_view name) {
        return std::ranges::find_if(info.properties, [name](const auto& p) { return p.name == name; });
    };

    const auto scaling = by_name("scaling");
    ASSERT_NE(scaling, info.properties.end());
    EXPECT_EQ(scaling->kind, atp::io::property_kind::text);
    EXPECT_EQ(scaling->default_value, "cover");
    EXPECT_EQ(scaling->options, (std::vector<std::string>{"none", "cover", "contain"}));

    const auto channels = by_name("channels");
    ASSERT_NE(channels, info.properties.end());
    EXPECT_EQ(channels->kind, atp::io::property_kind::number);
    EXPECT_EQ(channels->options, (std::vector<std::string>{"1", "2", "6"}));

    EXPECT_TRUE(by_name("limit")->options.empty());
}

TEST(ModuleManager, ReloadPluginReadsTheFileAgainAndRegistersItExactlyOnce) {
    atp::studio::module_manager manager;
    manager.load_plugin(ATP_TEST_PLUGIN);
    ASSERT_NE(manager.registry().find("plugin_module"), nullptr);

    EXPECT_TRUE(manager.reload_plugin(ATP_TEST_PLUGIN));

    const std::string stem = std::filesystem::path(ATP_TEST_PLUGIN).stem().string();
    const auto* info = find_plugin(manager, stem);
    ASSERT_NE(info, nullptr);
    EXPECT_TRUE(info->loaded);
    EXPECT_EQ(info->modules.size(), 2u);
    EXPECT_EQ(manager.plugins().size(), 1u);
    EXPECT_EQ(manager.registry().versions("plugin_module").size(), 1u);
    EXPECT_NE(manager.registry().find("plugin_module"), nullptr);
}

TEST(ModuleManager, UnloadPluginWithdrawsItsFactoriesAndItsRow) {
    atp::studio::module_manager manager;
    manager.load_plugin(ATP_TEST_PLUGIN);
    ASSERT_NE(manager.registry().find("plugin_module"), nullptr);

    EXPECT_TRUE(manager.unload_plugin(ATP_TEST_PLUGIN));

    EXPECT_TRUE(manager.plugins().empty());
    EXPECT_EQ(manager.registry().find("plugin_module"), nullptr);
    EXPECT_FALSE(manager.unload_plugin(ATP_TEST_PLUGIN));

    manager.load_plugin(ATP_TEST_PLUGIN);
    EXPECT_NE(manager.registry().find("plugin_module"), nullptr);
}

TEST(ModuleManager, TheFileAModuleWasDeclaredInIsReachableFromItsNameAndVersion) {
    atp::studio::module_manager manager;
    manager.load_plugin(ATP_TEST_PLUGIN_C);

    EXPECT_EQ(manager.module_source("c_probe", atp::version{2, 1}), "c_probe_declared_here.txt");
    EXPECT_TRUE(manager.module_source("c_bare", atp::version{1}).empty());
    EXPECT_TRUE(manager.module_source("c_probe", atp::version{9, 9}).empty());
    EXPECT_TRUE(manager.module_source("nobody", atp::version{1}).empty());

    EXPECT_TRUE(manager.unload_plugin(ATP_TEST_PLUGIN_C));
    EXPECT_TRUE(manager.module_source("c_probe", atp::version{2, 1}).empty());
}

TEST(ModuleManager, ReloadPluginOfAFileNeverLoadedChangesNothing) {
    atp::studio::module_manager manager;
    EXPECT_FALSE(manager.reload_plugin(ATP_TEST_PLUGIN));
    EXPECT_TRUE(manager.plugins().empty());
    EXPECT_EQ(manager.registry().find("plugin_module"), nullptr);
}

TEST(ModuleManager, PluginPathCarriesTheExtensionWhicheverWayTheFileWasNamed) {
    atp::studio::module_manager manager;
    std::filesystem::path without = ATP_TEST_PLUGIN;
    without.replace_extension();
    manager.load_plugin(without);
    manager.load_plugin(ATP_TEST_PLUGIN);

    ASSERT_EQ(manager.plugins().size(), 1u);
    EXPECT_EQ(manager.plugins().front().path.extension().string(), std::string(atp::runtime::plugin_extension));
    EXPECT_TRUE(manager.plugins().front().loaded);
}

TEST(StudioModuleManager, DescribeListsProperties) {
    atp::module_factory<propertied_probe> factory("propertied_probe");
    const atp::studio::module_info info = atp::studio::module_manager::describe(factory);
    ASSERT_EQ(info.properties.size(), 4u);
    const auto limit = std::ranges::find_if(info.properties, [](const auto& p) { return p.name == "limit"; });
    ASSERT_NE(limit, info.properties.end());
    EXPECT_EQ(limit->kind, atp::io::property_kind::number);
    EXPECT_EQ(limit->default_value, "10");
    EXPECT_TRUE(limit->persistent);
    const auto tmp = std::ranges::find_if(info.properties, [](const auto& p) { return p.name == "tmp"; });
    ASSERT_NE(tmp, info.properties.end());
    EXPECT_FALSE(tmp->persistent);
}

}  // namespace

namespace {

class handmade_broken : public atp::module_base {
   public:
    handmade_broken() {
        throw std::runtime_error("handmade constructor failed");
    }
    void initialize(atp::module_context&) override {}
    void start() override {}
    atp::work_status iterate(std::stop_token) override {
        return atp::work_status::idle;
    }
    void stop() override {}
    [[nodiscard]] atp::io::inputs& inputs() override {
        return in_;
    }
    [[nodiscard]] const atp::io::inputs& inputs() const override {
        return in_;
    }
    [[nodiscard]] atp::io::outputs& outputs() override {
        return out_;
    }
    [[nodiscard]] const atp::io::outputs& outputs() const override {
        return out_;
    }
    [[nodiscard]] atp::io::properties& properties() override {
        return props_;
    }
    [[nodiscard]] const atp::io::properties& properties() const override {
        return props_;
    }

   private:
    atp::io::inputs in_;
    atp::io::outputs out_;
    atp::io::properties props_;
};

}  // namespace

TEST(ModuleManager, AModuleWithoutAPortNodeIsStillProbedAndStillCanBeBroken) {
    atp::module_factory<handmade_broken> factory("handmade_broken");
    const atp::studio::module_info info = atp::studio::module_manager::describe(factory);

    EXPECT_TRUE(info.broken);
    EXPECT_NE(info.error.find("handmade constructor failed"), std::string::npos);
}

namespace {

void* declared_create(const atp_api*, atp_ctx*, void*) {
    return nullptr;
}

void declared_destroy(void*) {}

atp_work declared_iterate(void*) {
    return ATP_WORK_IDLE;
}

const char* const declared_engines[] = {"fm", "additive"};

const atp_config_field_desc declared_master_fields[] = {
    {"gain", ATP_FIELD_REAL, "1.0", nullptr, 0, ATP_FIELD_STRING, nullptr, 0},
};

const atp_config_field_desc declared_fields[] = {
    {"rate", ATP_FIELD_INT, nullptr, nullptr, 0, ATP_FIELD_STRING, nullptr, 0},
    {"engine", ATP_FIELD_STRING, "fm", declared_engines, 2, ATP_FIELD_STRING, nullptr, 0},
    {"master", ATP_FIELD_OBJECT, nullptr, nullptr, 0, ATP_FIELD_STRING, declared_master_fields, 1},
};

atp_module_desc make_desc(const atp_config_field_desc* fields, std::uint32_t count) {
    atp_module_desc desc{};
    desc.struct_size = sizeof(atp_module_desc);
    desc.name = "c_described";
    desc.version[0] = 1;
    desc.version_count = 1;
    desc.create = declared_create;
    desc.destroy = declared_destroy;
    desc.iterate = declared_iterate;
    desc.config_fields = fields;
    desc.config_field_count = count;
    return desc;
}

}  // namespace

TEST(ModuleManager, ACModuleDeclaringFieldsKeepsItsConfigSchema) {
    const atp_module_desc desc = make_desc(declared_fields, 3);
    const atp::runtime::c_module_factory factory(desc);
    const atp::studio::module_info info = atp::studio::module_manager::describe(factory);

    EXPECT_FALSE(info.broken) << info.error;
    EXPECT_TRUE(info.takes_config);
    ASSERT_NE(info.config_schema, nullptr) << "a declared config is what the inspector draws a tree from";
    ASSERT_EQ(info.config_schema->entries().size(), 3U);
    EXPECT_EQ(info.config_schema->entries()[0].name(), "rate");
    EXPECT_EQ(info.config_schema->find("engine")->options(), (std::vector<std::string>{"fm", "additive"}));
}

TEST(ModuleManager, ACModuleDeclaringNoFieldsTakesAConfigWithoutDescribingIt) {
    const atp_module_desc desc = make_desc(nullptr, 0);
    const atp::runtime::c_module_factory factory(desc);
    const atp::studio::module_info info = atp::studio::module_manager::describe(factory);

    EXPECT_TRUE(info.takes_config);
    EXPECT_EQ(info.config_schema, nullptr) << "a raw_config declares nothing, so there is no tree to draw";
}
