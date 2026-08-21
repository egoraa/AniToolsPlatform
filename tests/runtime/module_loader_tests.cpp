// SPDX-License-Identifier: Apache-2.0
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <atp/module.hpp>
#include <atp/runtime/module_loader.hpp>

namespace {

class host_module : public atp::module<atp::ports<>, "", atp::version{1, 0}> {};

}  // namespace

TEST(ModuleLoader, LoadsAndRegisters) {
    atp::module_registry registry;
    atp::runtime::module_loader loader{ATP_TEST_PLUGIN, registry};
    EXPECT_EQ(loader.modules(),
              (std::vector<atp::runtime::registered_module>{{"plugin_module", atp::version(2, 0), ""},
                                                            {"plugin_alias", atp::version(2, 0), ""}}));

    auto module = registry.create("plugin_module");
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->get_version(), atp::version(2, 0));
    module.reset();
}

TEST(ModuleLoader, VersionAvailableWithoutInstantiation) {
    atp::module_registry registry;
    atp::runtime::module_loader loader{ATP_TEST_PLUGIN, registry};
    EXPECT_EQ(registry.at("plugin_alias").get_version(), atp::version(2, 0));
}

TEST(ModuleLoader, UnloadRemovesFactories) {
    atp::module_registry registry;
    {
        atp::runtime::module_loader loader{ATP_TEST_PLUGIN, registry};
        EXPECT_NE(registry.find("plugin_module"), nullptr);
        EXPECT_NE(registry.find("plugin_alias"), nullptr);
    }
    EXPECT_EQ(registry.find("plugin_module"), nullptr);
    EXPECT_EQ(registry.find("plugin_alias"), nullptr);
}

TEST(ModuleLoader, UnloadKeepsHostVersionOfSameName) {
    atp::module_registry registry;
    registry.add<host_module>("plugin_module");
    {
        atp::runtime::module_loader loader{ATP_TEST_PLUGIN, registry};
        EXPECT_EQ(registry.at("plugin_module").get_version(), atp::version(2, 0));
    }
    ASSERT_NE(registry.find("plugin_module"), nullptr);
    EXPECT_EQ(registry.at("plugin_module").get_version(), atp::version(1, 0));
}

TEST(ModuleLoader, ModuleOutlivesLoader) {
    atp::module_registry registry;
    atp::module_ptr module;
    {
        atp::runtime::module_loader loader{ATP_TEST_PLUGIN, registry};
        module = registry.create("plugin_module");
    }
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->get_version(), atp::version(2, 0));
    module->iterate(std::stop_token{});
    module.reset();
}

TEST(ModuleLoader, FactoriesRemovedOnUnloadEvenWithLiveModule) {
    atp::module_registry registry;
    atp::module_ptr module;
    {
        atp::runtime::module_loader loader{ATP_TEST_PLUGIN, registry};
        module = registry.create("plugin_module");
    }
    EXPECT_EQ(registry.find("plugin_module"), nullptr);
    EXPECT_NE(module, nullptr);
}

TEST(ModuleLoader, LoadsPathWithoutExtensionAppendingPlatformOne) {
    atp::module_registry registry;
    std::filesystem::path bare(ATP_TEST_PLUGIN);
    bare.replace_extension();
    atp::runtime::module_loader loader(bare, registry);
    EXPECT_NE(registry.find("plugin_module"), nullptr);
}

TEST(ModuleLoader, MissingFileThrows) {
    atp::module_registry registry;
    EXPECT_THROW((atp::runtime::module_loader{"no_such_plugin.dll", registry}), std::runtime_error);
    EXPECT_TRUE(registry.list().empty());
}

TEST(ModuleLoader, EmptyPluginReportsMissingSymbol) {
    atp::module_registry registry;
    try {
        atp::runtime::module_loader loader{ATP_TEST_PLUGIN_EMPTY, registry};
        FAIL() << "expected std::runtime_error";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find("atp_abi_version"), std::string::npos);
    }
    EXPECT_TRUE(registry.list().empty());
}

TEST(ModuleLoader, AbiMismatchReportsVersions) {
    atp::module_registry registry;
    try {
        atp::runtime::module_loader loader{ATP_TEST_PLUGIN_BAD_ABI, registry};
        FAIL() << "expected std::runtime_error";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find("ABI"), std::string::npos);
    }
    EXPECT_TRUE(registry.list().empty());
}

TEST(ModuleLoader, ForeignLibraryIsItsOwnFailure) {
    atp::module_registry registry;
    EXPECT_THROW(atp::runtime::module_loader(ATP_TEST_PLUGIN_EMPTY, registry), atp::runtime::not_a_plugin);
}

TEST(ModuleLoader, AbiMismatchIsNotAForeignLibrary) {
    atp::module_registry registry;
    try {
        atp::runtime::module_loader loader{ATP_TEST_PLUGIN_BAD_ABI, registry};
        FAIL() << "expected a failure";
    } catch (const atp::runtime::not_a_plugin&) {
        FAIL() << "a wrong ABI is a broken plugin, not a foreign library";
    } catch (const std::runtime_error&) {
    }
}

TEST(ModuleLoader, MoveConstructorTransfersOwnership) {
    atp::module_registry registry;
    atp::runtime::module_loader first{ATP_TEST_PLUGIN, registry};
    atp::runtime::module_loader second{std::move(first)};
    EXPECT_NE(registry.find("plugin_module"), nullptr);
    EXPECT_EQ(second.modules().size(), 2u);
}

TEST(ModuleLoader, MoveAssignmentUnloadsTarget) {
    atp::module_registry first_registry;
    atp::module_registry second_registry;
    atp::runtime::module_loader source{ATP_TEST_PLUGIN, first_registry};
    atp::runtime::module_loader target{ATP_TEST_PLUGIN, second_registry};

    target = std::move(source);
    EXPECT_EQ(second_registry.find("plugin_module"), nullptr);
    EXPECT_NE(first_registry.find("plugin_module"), nullptr);
}
