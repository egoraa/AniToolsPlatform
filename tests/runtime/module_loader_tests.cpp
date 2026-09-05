// SPDX-License-Identifier: Apache-2.0
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <atp/module.hpp>
#include <atp/runtime/module_loader.hpp>
#include <atp/runtime/utf8_path.hpp>

namespace {

class host_module : public atp::module<atp::ports<>, "", atp::version{1, 0}> {};

std::filesystem::path unicode_path(std::initializer_list<char32_t> points, std::string_view suffix) {
    std::u32string name(points);
    for (const char c : suffix) {
        name.push_back(static_cast<char32_t>(c));
    }
    return {name};
}

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
        SUCCEED();
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

TEST(ModuleLoader, NamesAMissingPluginInUtf8) {
    atp::module_registry registry;
    const std::filesystem::path missing =
        std::filesystem::temp_directory_path() / unicode_path({0x043d, 0x0435, 0x0442, 0x0443}, ".dll");
    try {
        const atp::runtime::module_loader loader{missing, registry};
        FAIL() << "a missing plugin must throw";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find(atp::runtime::path_to_utf8(missing.filename())), std::string::npos)
            << "path::string() spells the name in the process code page, and every string here is UTF-8";
    }
}

TEST(ModuleLoader, ForeignLibraryNamedOutsideAsciiStaysNotAPlugin) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "atp_loader_utf8";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::filesystem::path copy =
        dir / unicode_path({0x0447, 0x0443, 0x0436, 0x0430, 0x044f}, atp::runtime::plugin_extension);
    std::filesystem::copy_file(ATP_TEST_PLUGIN_EMPTY, copy);

    atp::module_registry registry;
    try {
        const atp::runtime::module_loader loader{copy, registry};
        FAIL() << "a library exporting neither entry point is not a plugin";
    } catch (const atp::runtime::not_a_plugin& error) {
        EXPECT_NE(std::string(error.what()).find(atp::runtime::path_to_utf8(copy.filename())), std::string::npos);
    }
}

TEST(ModuleLoader, RegistrationFailureNamesThePluginFile) {
    atp::module_registry registry;
    const atp::runtime::module_loader first{ATP_TEST_PLUGIN, registry};
    try {
        const atp::runtime::module_loader second{ATP_TEST_PLUGIN, registry};
        FAIL() << "the same plugin twice in one registry is a duplicate registration";
    } catch (const std::runtime_error& error) {
        const std::string text = error.what();
        EXPECT_NE(text.find("duplicate"), std::string::npos);
        EXPECT_NE(text.find(atp::runtime::path_to_utf8(std::filesystem::path(ATP_TEST_PLUGIN).filename())),
                  std::string::npos)
            << "the C++ path used to drop the file name that the C path adds";
    }
}
