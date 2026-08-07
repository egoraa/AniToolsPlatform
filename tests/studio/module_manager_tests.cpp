// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <array>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeindex>
#include <typeinfo>
#include <vector>

#include <gtest/gtest.h>

#include <atp/module.hpp>
#include <atp/module_factory.hpp>
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
using probe_ports = atp::io::ports<probe_inputs, probe_outputs>;
class probed_module : public atp::module<probe_ports, "probed"> {};

class throwing_module : public atp::module<atp::io::ports<>, "throwing"> {
   public:
    throwing_module() {
        throw std::runtime_error("broken constructor");
    }
};

TEST(ModuleManager, DescribeProbesPortsAndSurvivesBrokenConstructors) {
    atp::module_factory<probed_module> good("probed");
    const atp::studio::module_info info = atp::studio::module_manager::describe(good);
    EXPECT_FALSE(info.broken);
    ASSERT_EQ(info.inputs.size(), 1u);
    EXPECT_EQ(info.inputs[0].name, "value");
    EXPECT_EQ(info.inputs[0].type, std::type_index(typeid(int)));
    ASSERT_EQ(info.outputs.size(), 1u);
    EXPECT_EQ(info.outputs[0].name, "count");

    atp::module_factory<throwing_module> bad("throwing");
    const atp::studio::module_info broken = atp::studio::module_manager::describe(bad);
    EXPECT_TRUE(broken.broken);
    EXPECT_NE(broken.error.find("broken constructor"), std::string::npos);
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
    : public atp::module<atp::io::ports<atp::io::inputs, atp::io::outputs, probe_props>, "propertied_probe"> {};

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
