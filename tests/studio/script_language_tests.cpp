// SPDX-License-Identifier: Apache-2.0
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <atp/studio/languages.hpp>
#include <atp/studio/module_manager.hpp>
#include <atp/studio/script_modules.hpp>

namespace {

using atp::studio::script_language;

std::filesystem::path fresh_dir(const char* leaf) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "atp_script_language_tests" / leaf;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

void make_package(const std::filesystem::path& scripts, const script_language& lang, const char* body) {
    if (lang.package_is_directory) {
        std::filesystem::create_directories(scripts / lang.package_entry);
        std::ofstream(scripts / lang.package_entry / ("__init__" + std::string(lang.file_extension))) << body;
    } else {
        std::filesystem::create_directories(scripts);
        std::ofstream(scripts / lang.package_entry) << body;
    }
}

atp::studio::bridge_source fake_source(const std::filesystem::path& from, const script_language& lang) {
    std::filesystem::create_directories(from);
    std::ofstream(from / atp::studio::bridge_filename(lang)) << "not really a library";
    make_package(from / lang.scripts_subdir, lang, "-- package\n");

    atp::studio::bridge_source source;
    source.bridge = from / atp::studio::bridge_filename(lang);
    source.package = from / lang.scripts_subdir / lang.package_entry;
    return source;
}

void age_package(const std::filesystem::path& package, const script_language& lang, std::chrono::hours by) {
    if (!lang.package_is_directory) {
        std::filesystem::last_write_time(package, std::filesystem::last_write_time(package) - by);
        return;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(package)) {
        if (entry.is_regular_file() && entry.path().extension() == lang.file_extension) {
            std::filesystem::last_write_time(entry.path(), std::filesystem::last_write_time(entry.path()) - by);
        }
    }
}

class script_language_test : public ::testing::TestWithParam<const script_language*> {
   protected:
    [[nodiscard]] static const script_language& lang() {
        return *GetParam();
    }
};

INSTANTIATE_TEST_SUITE_P(EveryLanguage,
                         script_language_test,
                         ::testing::Values(&atp::studio::python_language, &atp::studio::lua_language),
                         [](const ::testing::TestParamInfo<const script_language*>& param_info) {
                             return std::string(param_info.param->label);
                         });

TEST_P(script_language_test, TheSkeletonItWritesNamesTheModuleItDeclares) {
    const std::string text = lang().render_skeleton("x_sample");
    EXPECT_NE(text.find("x_sample"), std::string::npos) << text;
}

TEST_P(script_language_test, ANameThatWouldShadowThePackageIsRefused) {
    EXPECT_FALSE(lang().name_valid("atp")) << "a module of that name would collide with the package itself";
    EXPECT_FALSE(lang().name_valid("1st"));
    EXPECT_FALSE(lang().name_valid(""));
    EXPECT_FALSE(lang().name_valid("has space"));
    EXPECT_TRUE(lang().name_valid("sample"));
}

TEST_P(script_language_test, EveryLanguageHasItsOwnSubdirectoryAndVariable) {
    for (const script_language& other : atp::studio::languages()) {
        if (other.id == lang().id) {
            continue;
        }
        EXPECT_NE(other.scripts_subdir, lang().scripts_subdir);
        EXPECT_NE(other.path_variable, lang().path_variable);
        EXPECT_NE(other.bridge_stem, lang().bridge_stem);
        EXPECT_NE(other.file_extension, lang().file_extension);
    }
}

TEST_P(script_language_test, ScriptDirectoriesAreDerivedFromTheSearchDirectories) {
    const std::filesystem::path root = fresh_dir("derive");
    std::filesystem::create_directories(root / "with" / lang().scripts_subdir);
    std::filesystem::create_directories(root / "without");

    const std::vector<std::filesystem::path> dirs{root / "with", root / "without"};
    EXPECT_EQ(atp::studio::derive_script_dirs(dirs, lang()),
              (std::vector<std::string>{(root / "with" / lang().scripts_subdir).string()}));
    EXPECT_EQ(atp::studio::last_script_folder(dirs, lang()), root / "with");

    std::filesystem::remove_all(root);
}

TEST_P(script_language_test, CreatingWritesTheSkeletonAndRefusesToTouchAnExistingFile) {
    const std::filesystem::path dir = fresh_dir("create");
    const std::string name = std::string(lang().name_prefix) + "thing";

    const std::filesystem::path file = atp::studio::create_script_module(dir, name, lang());
    EXPECT_EQ(file, dir / (name + std::string(lang().file_extension)));
    std::stringstream body;
    {
        std::ifstream read(file);
        body << read.rdbuf();
    }
    EXPECT_EQ(body.str(), lang().render_skeleton(name));

    EXPECT_THROW((void)atp::studio::create_script_module(dir, name, lang()), std::runtime_error);
    EXPECT_THROW((void)atp::studio::create_script_module(dir, "1bad", lang()), std::runtime_error);

    std::filesystem::remove_all(dir);
}

TEST_P(script_language_test, ProvisioningCopiesTheBridgeAndThePackageOnlyWhenTheyAreMissing) {
    const std::filesystem::path root = fresh_dir("provision");
    const atp::studio::bridge_source source = fake_source(root / "from", lang());
    const std::filesystem::path folder = root / "modules";

    const atp::studio::folder_setup first = atp::studio::provision_folder(folder, source, lang());
    EXPECT_EQ(first.scripts_dir, folder / lang().scripts_subdir);
    EXPECT_TRUE(first.bridge_copied);
    EXPECT_TRUE(first.package_copied);
    EXPECT_TRUE(std::filesystem::exists(folder / atp::studio::bridge_filename(lang())));
    EXPECT_TRUE(std::filesystem::exists(folder / lang().scripts_subdir / lang().package_entry));

    const atp::studio::folder_setup again = atp::studio::provision_folder(folder, source, lang());
    EXPECT_FALSE(again.bridge_copied);
    EXPECT_FALSE(again.package_copied);
    EXPECT_FALSE(again.package_refreshed);

    std::filesystem::remove_all(root);
}

TEST_P(script_language_test, AStalePackageIsRefreshedWhateverShapeItHas) {
    const std::filesystem::path root = fresh_dir("refresh");
    const atp::studio::bridge_source source = fake_source(root / "from", lang());
    const std::filesystem::path folder = root / "modules";
    (void)atp::studio::provision_folder(folder, source, lang());

    age_package(folder / lang().scripts_subdir / lang().package_entry, lang(), std::chrono::hours(2));
    const atp::studio::folder_setup refreshed = atp::studio::provision_folder(folder, source, lang());
    EXPECT_TRUE(refreshed.package_refreshed)
        << "a copy older than the platform's own has to follow the bridge it belongs to";
    EXPECT_FALSE(refreshed.bridge_copied) << "the bridge file is only ever created, never replaced";

    std::filesystem::remove_all(root);
}

TEST_P(script_language_test, OneFolderCanCarryTwoLanguagesAtOnce) {
    const std::filesystem::path root = fresh_dir("shared");
    const std::filesystem::path folder = root / "modules";
    for (const script_language& one : atp::studio::languages()) {
        const atp::studio::bridge_source source = fake_source(root / std::string(one.id), one);
        (void)atp::studio::provision_folder(folder, source, one);
    }
    for (const script_language& one : atp::studio::languages()) {
        EXPECT_TRUE(std::filesystem::exists(folder / one.scripts_subdir / one.package_entry))
            << one.label << " lost its package to the other language";
    }
    std::filesystem::remove_all(root);
}

TEST(LuaLanguage, ADigitLeadingNameIsRefusedButAnUnderscoreOneIsNot) {
    EXPECT_FALSE(atp::studio::lua_language.name_valid("1st"));
    EXPECT_TRUE(atp::studio::lua_language.name_valid("_1"))
        << "nothing is derived from the name here, so the rule Python needs does not apply";
    EXPECT_FALSE(atp::studio::python_language.name_valid("_1"));
}

TEST(LuaLanguage, ItsPackageIsAFileRatherThanADirectory) {
    EXPECT_FALSE(atp::studio::lua_language.package_is_directory);
    EXPECT_EQ(atp::studio::lua_language.package_entry, "atp.lua");
    EXPECT_TRUE(atp::studio::python_language.package_is_directory);
}

TEST(LuaLanguage, ItsOnlyDifferenceFromPythonIsTheMissingDependencyHint) {
    EXPECT_TRUE(atp::studio::lua_language.missing_dependency_hint.empty())
        << "the interpreter is inside the plugin, so there is no absent runtime to hint at";
    EXPECT_FALSE(atp::studio::python_language.missing_dependency_hint.empty());
}

TEST_P(script_language_test, KeepingOneBridgeIsNotALanguageTrait) {
    atp::studio::module_manager manager;
    EXPECT_TRUE(atp::studio::keep_one_bridge(manager, lang()).empty()) << "nothing loaded, nothing to drop";
    EXPECT_NE(atp::studio::dropped_bridge_note(std::filesystem::path("x") / "y", lang())
                  .find("registers the module names the first already holds"),
              std::string::npos)
        << "the reason a second copy goes is one registry and one search-directory list, which is true "
           "of every bridge that reads files — CPython's Ctx singleton is a stronger reason for the "
           "same rule, not a different rule";
}

}  // namespace
