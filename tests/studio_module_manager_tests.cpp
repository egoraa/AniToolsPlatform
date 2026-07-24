#include <filesystem>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <typeinfo>

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
    ASSERT_EQ(good->modules.size(), 2u);  // plugin_module + алиас plugin_alias
    EXPECT_NE(manager.registry().find("plugin_module"), nullptr);

    // «empty» — DLL без atp-контракта (нет atp_abi_version): не плагин,
    // менеджер показывает отказ с причиной, а не грузит молча
    const auto* empty = find_plugin(manager, std::filesystem::path(ATP_TEST_PLUGIN_EMPTY).stem().string());
    ASSERT_NE(empty, nullptr);
    EXPECT_FALSE(empty->loaded);
    EXPECT_NE(empty->error.find("atp_abi_version"), std::string::npos);

    const auto* bad = find_plugin(manager, std::filesystem::path(ATP_TEST_PLUGIN_BAD_ABI).stem().string());
    ASSERT_NE(bad, nullptr);
    EXPECT_FALSE(bad->loaded);
    EXPECT_NE(bad->error.find("ABI"), std::string::npos);  // причина отказа видна

    // повторный скан не дублирует уже загруженное
    manager.rescan();
    EXPECT_EQ(manager.registry().versions("plugin_module").size(), 1u);
}

TEST(ModuleManager, DuplicateModulesRejectPluginEntirely) {
    atp::studio::module_manager manager;
    manager.load_plugin(ATP_TEST_PLUGIN);

    // та же DLL под другим именем: те же (имя, версия) → конфликт, файл
    // отвергается целиком, победил загруженный раньше
    const auto copy = std::filesystem::temp_directory_path() / "copy_of_test_plugin.dll";
    std::filesystem::copy_file(ATP_TEST_PLUGIN, copy, std::filesystem::copy_options::overwrite_existing);
    manager.load_plugin(copy);

    const auto* dup = find_plugin(manager, "copy_of_test_plugin");
    ASSERT_NE(dup, nullptr);
    EXPECT_FALSE(dup->loaded);
    EXPECT_NE(dup->error.find("duplicate"), std::string::npos);
    EXPECT_EQ(manager.registry().versions("plugin_module").size(), 1u);
}

// Порты палитра узнаёт пробным экземпляром: конструктор лёгкий по
// контракту жизненного цикла (тяжёлое — в initialize, он не зовётся).
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
