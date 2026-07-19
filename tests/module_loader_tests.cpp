#include <memory>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <atp/module.hpp>
#include <atp/module_loader.hpp>

// Пути к тестовым плагинам приходят из CMake (см. tests/CMakeLists.txt).

namespace {

// Хостовая версия имени, которое регистрирует и тестовый плагин (2.0), —
// для проверки, что выгрузка плагина не задевает чужие версии.
class host_module : public atp::module<atp::io::inputs, atp::io::outputs, "", atp::version{1, 0}> {};

}  // namespace

TEST(ModuleLoader, LoadsAndRegisters) {
    atp::module_registry registry;
    atp::module_loader loader{ATP_TEST_PLUGIN, registry};
    EXPECT_EQ(loader.modules(), (std::vector<std::pair<std::string, atp::version>>{
                                    {"plugin_module", atp::version(2, 0)}, {"plugin_alias", atp::version(2, 0)}}));

    auto module = registry.create("plugin_module");
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->get_version(), atp::version(2, 0));
    module.reset();  // порядок не обязателен (пин), но локальный объект чистим сами
}

TEST(ModuleLoader, VersionAvailableWithoutInstantiation) {
    atp::module_registry registry;
    atp::module_loader loader{ATP_TEST_PLUGIN, registry};
    EXPECT_EQ(registry.at("plugin_alias").get_version(), atp::version(2, 0));
}

TEST(ModuleLoader, UnloadRemovesFactories) {
    atp::module_registry registry;
    {
        atp::module_loader loader{ATP_TEST_PLUGIN, registry};
        EXPECT_NE(registry.find("plugin_module"), nullptr);
        EXPECT_NE(registry.find("plugin_alias"), nullptr);
    }
    // после разрушения загрузчика его фабрик в реестре нет
    EXPECT_EQ(registry.find("plugin_module"), nullptr);
    EXPECT_EQ(registry.find("plugin_alias"), nullptr);
}

TEST(ModuleLoader, UnloadKeepsHostVersionOfSameName) {
    atp::module_registry registry;
    registry.add<host_module>("plugin_module");  // хостовая 1.0
    {
        atp::module_loader loader{ATP_TEST_PLUGIN, registry};
        // пока плагин загружен, последняя версия имени — плагинная 2.0
        EXPECT_EQ(registry.at("plugin_module").get_version(), atp::version(2, 0));
    }
    // выгрузка сняла только пару ("plugin_module", 2.0); хостовая 1.0 на месте
    ASSERT_NE(registry.find("plugin_module"), nullptr);
    EXPECT_EQ(registry.at("plugin_module").get_version(), atp::version(1, 0));
}

TEST(ModuleLoader, ModuleOutlivesLoader) {
    atp::module_registry registry;
    atp::module_ptr module;
    {
        atp::module_loader loader{ATP_TEST_PLUGIN, registry};
        module = registry.create("plugin_module");
    }
    // Загрузчик мёртв, но модуль пинит библиотеку — код доступен.
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->get_version(), atp::version(2, 0));
    module->iterate(std::stop_token{});
    module.reset();  // последний пин: здесь библиотека выгружается физически
}

TEST(ModuleLoader, FactoriesRemovedOnUnloadEvenWithLiveModule) {
    atp::module_registry registry;
    atp::module_ptr module;
    {
        atp::module_loader loader{ATP_TEST_PLUGIN, registry};
        module = registry.create("plugin_module");
    }
    EXPECT_EQ(registry.find("plugin_module"), nullptr);  // фабрик уже нет
    EXPECT_NE(module, nullptr);                          // а модуль жив
}

TEST(ModuleLoader, MissingFileThrows) {
    atp::module_registry registry;
    EXPECT_THROW((atp::module_loader{"no_such_plugin.dll", registry}), std::runtime_error);
    EXPECT_TRUE(registry.list().empty());
}

TEST(ModuleLoader, EmptyPluginReportsMissingSymbol) {
    atp::module_registry registry;
    try {
        atp::module_loader loader{ATP_TEST_PLUGIN_EMPTY, registry};
        FAIL() << "expected std::runtime_error";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find("atp_abi_version"), std::string::npos);
    }
    EXPECT_TRUE(registry.list().empty());
}

TEST(ModuleLoader, AbiMismatchReportsVersions) {
    atp::module_registry registry;
    try {
        atp::module_loader loader{ATP_TEST_PLUGIN_BAD_ABI, registry};
        FAIL() << "expected std::runtime_error";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find("ABI"), std::string::npos);
    }
    EXPECT_TRUE(registry.list().empty());
}

TEST(ModuleLoader, MoveConstructorTransfersOwnership) {
    atp::module_registry registry;
    atp::module_loader first{ATP_TEST_PLUGIN, registry};
    atp::module_loader second{std::move(first)};
    EXPECT_NE(registry.find("plugin_module"), nullptr);
    EXPECT_EQ(second.modules().size(), 2u);
}  // разрушение обоих: пустой first ничего не снимает, second выгружает

TEST(ModuleLoader, MoveAssignmentUnloadsTarget) {
    atp::module_registry first_registry;
    atp::module_registry second_registry;
    atp::module_loader source{ATP_TEST_PLUGIN, first_registry};
    atp::module_loader target{ATP_TEST_PLUGIN, second_registry};

    target = std::move(source);
    // target сначала снял свои фабрики из second_registry, затем принял source
    EXPECT_EQ(second_registry.find("plugin_module"), nullptr);
    EXPECT_NE(first_registry.find("plugin_module"), nullptr);
}
