// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <QString>
#include <QStringList>

#include "model/editor.hpp"
#include "model/new_script_module.hpp"
#include "ui/qt_app.hpp"

namespace {

using atp::studio::ui::app_state;
using atp::studio::ui::create_script_module_action;
using atp::studio::ui::editor_arguments;
using atp::studio::ui::ui_callbacks;

struct harness {
    app_state state;
    ui_callbacks callbacks;
    std::vector<std::pair<std::string, atp::log_level>> log;
    std::filesystem::path dir;

    explicit harness(const char* leaf) {
        dir = std::filesystem::temp_directory_path() / "atp_new_script_module_tests" / leaf;
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        state.settings_file = dir / "settings.json";
        callbacks.project_changed = [] {};
        callbacks.error = [](const QString&) {};
        callbacks.selection_changed = [] {};
        callbacks.report = [this](const QString& text, atp::log_level level) {
            log.emplace_back(text.toStdString(), level);
        };
    }

    ~harness() {
        std::error_code ignored;
        std::filesystem::remove_all(dir, ignored);
    }

    [[nodiscard]] atp::studio::bridge_source source_for(const atp::studio::script_language& lang) const {
        const std::filesystem::path from = dir.parent_path() / ("from_" + std::string(lang.id));
        const std::filesystem::path scripts = from / lang.scripts_subdir;
        std::filesystem::create_directories(scripts);
        std::ofstream(from / atp::studio::bridge_filename(lang)) << "not really a library";
        if (lang.package_is_directory) {
            std::filesystem::create_directories(scripts / lang.package_entry);
            std::ofstream(scripts / lang.package_entry / ("__init__" + std::string(lang.file_extension)))
                << "-- package\n";
        } else {
            std::ofstream(scripts / lang.package_entry) << "-- package\n";
        }
        atp::studio::bridge_source ready;
        ready.bridge = from / atp::studio::bridge_filename(lang);
        ready.package = scripts / lang.package_entry;
        return ready;
    }

    [[nodiscard]] atp::studio::bridge_source source() const {
        const std::filesystem::path from = dir.parent_path() / "from";
        std::filesystem::create_directories(from / "python" / "atp");
        std::ofstream(from / atp::studio::bridge_filename(atp::studio::python_language)) << "not really a library";
        std::ofstream(from / "python" / "atp" / "__init__.py") << "# package\n";
        atp::studio::bridge_source ready;
        ready.bridge = from / atp::studio::bridge_filename(atp::studio::python_language);
        ready.package = from / "python" / "atp";
        return ready;
    }

    harness(const harness&) = delete;
    harness& operator=(const harness&) = delete;
    harness(harness&&) = delete;
    harness& operator=(harness&&) = delete;

    [[nodiscard]] bool said(const std::string& fragment, atp::log_level level) const {
        return std::ranges::any_of(log, [&fragment, level](const std::pair<std::string, atp::log_level>& line) {
            return line.second == level && line.first.contains(fragment);
        });
    }
};

TEST(UiNewScriptModule, TheScriptLandsBesideThePackageAndTheScriptsDirectoryIsRemembered) {
    harness h("created");

    const auto file = create_script_module_action(h.state, h.callbacks, atp::studio::python_language, h.dir,
                                                  "py_studio_made", h.source());

    ASSERT_TRUE(file.has_value());
    EXPECT_EQ(*file, h.dir / "python" / "py_studio_made.py");
    EXPECT_TRUE(std::filesystem::exists(*file));
    EXPECT_TRUE(std::filesystem::exists(h.dir / atp::studio::bridge_filename(atp::studio::python_language)));
    EXPECT_TRUE(std::filesystem::exists(h.dir / "python" / "atp" / "__init__.py"));
    ASSERT_EQ(h.state.manager.search_dirs().size(), 1u);
    EXPECT_EQ(h.state.manager.search_dirs().front(), h.dir);
    EXPECT_EQ(atp::studio::derive_script_dirs(h.state.manager.search_dirs(), atp::studio::python_language),
              (std::vector<std::string>{(h.dir / "python").string()}));
    EXPECT_TRUE(std::filesystem::exists(h.state.settings_file));
    EXPECT_TRUE(h.said("copied", atp::log_level::info));
    EXPECT_TRUE(h.said("module search directory now", atp::log_level::info));
}

TEST(UiNewScriptModule, WithoutABridgeToCopyItSaysWhereItLooked) {
    harness h("no_source");

    const auto file = create_script_module_action(h.state, h.callbacks, atp::studio::python_language, h.dir,
                                                  "py_studio_made", atp::studio::bridge_source{});

    ASSERT_TRUE(file.has_value());
    EXPECT_TRUE(std::filesystem::exists(*file));
    EXPECT_FALSE(std::filesystem::exists(h.dir / atp::studio::bridge_filename(atp::studio::python_language)));
    EXPECT_TRUE(h.said("no " + atp::studio::bridge_filename(atp::studio::python_language) + " to copy",
                       atp::log_level::warning));
}

TEST(UiNewScriptModule, ABridgeThatFailsToLoadIsQuotedRatherThanHintedAt) {
    harness h("failed_bridge");

    const auto file = create_script_module_action(h.state, h.callbacks, atp::studio::python_language, h.dir,
                                                  "py_studio_made", h.source());

    ASSERT_TRUE(file.has_value());
    EXPECT_TRUE(h.said("did not load", atp::log_level::error));
    EXPECT_TRUE(h.said("cannot load plugin", atp::log_level::error));
    EXPECT_TRUE(h.said(atp::studio::bridge_filename(atp::studio::python_language), atp::log_level::error));
}

TEST(UiNewScriptModule, AMissingDependencyIsCalledOutAsSuchRatherThanLeftAsTheSystemWordsIt) {
    EXPECT_TRUE(atp::studio::reads_as_missing_dependency(
        "cannot load plugin 'x.dll': The specified module could not be found."));
    EXPECT_TRUE(
        atp::studio::reads_as_missing_dependency("cannot load plugin 'x.so': libpython3.so: cannot open shared "
                                                 "object file: No such file or directory"));
    EXPECT_FALSE(atp::studio::reads_as_missing_dependency("plugin 'x.dll': duplicate module 'py_a' version '1.0'"));
    EXPECT_FALSE(atp::studio::reads_as_missing_dependency("plugin 'x.dll' has no symbol 'atp_abi_version'"));
}

TEST(UiNewScriptModule, ASecondModuleOfTheSameNameIsRefusedAndTheDirectoryIsNotListedTwice) {
    harness h("twice");
    ASSERT_TRUE(create_script_module_action(h.state, h.callbacks, atp::studio::python_language, h.dir, "py_studio_made",
                                            h.source())
                    .has_value());

    const auto second = create_script_module_action(h.state, h.callbacks, atp::studio::python_language, h.dir,
                                                    "py_studio_made", h.source());

    EXPECT_FALSE(second.has_value());
    EXPECT_TRUE(h.said("already exists", atp::log_level::error));
    EXPECT_EQ(h.state.manager.search_dirs().size(), 1u);
}

TEST(UiNewScriptModule, TheEditorCommandKeepsAProgramWithSpacesWholeAndSubstitutesThePath) {
    const QStringList plain = editor_arguments(QStringLiteral("notepad"), QStringLiteral("c:/x/y.py"));
    ASSERT_EQ(plain.size(), 2);
    EXPECT_EQ(plain.at(0), QStringLiteral("notepad"));
    EXPECT_EQ(plain.at(1), QStringLiteral("c:/x/y.py"));

    const QStringList placed = editor_arguments(QStringLiteral("code -g \"{file}\""), QStringLiteral("c:/a b/y.py"));
    ASSERT_EQ(placed.size(), 3);
    EXPECT_EQ(placed.at(0), QStringLiteral("code"));
    EXPECT_EQ(placed.at(1), QStringLiteral("-g"));
    EXPECT_EQ(placed.at(2), QStringLiteral("c:/a b/y.py"));

    const QStringList quoted =
        editor_arguments(QStringLiteral("\"C:/Program Files/E/e.exe\" {file}"), QStringLiteral("c:/x/y.py"));
    ASSERT_EQ(quoted.size(), 2);
    EXPECT_EQ(quoted.at(0), QStringLiteral("C:/Program Files/E/e.exe"));
    EXPECT_EQ(quoted.at(1), QStringLiteral("c:/x/y.py"));
}

TEST(UiNewScriptModule, ALuaModuleLandsInItsOwnSubdirectoryBesideItsOwnPackage) {
    harness h("lua_module");
    const atp::studio::script_language& lang = atp::studio::lua_language;

    const auto file =
        create_script_module_action(h.state, h.callbacks, lang, h.dir, "lua_studio_made", h.source_for(lang));

    ASSERT_TRUE(file.has_value());
    EXPECT_EQ(file->parent_path().filename().string(), lang.scripts_subdir);
    EXPECT_EQ(file->extension().string(), lang.file_extension);
    EXPECT_TRUE(std::filesystem::exists(h.dir / lang.scripts_subdir / lang.package_entry))
        << "the package of this language is one file, and it has to be copied like one";
    EXPECT_TRUE(std::filesystem::exists(h.dir / atp::studio::bridge_filename(lang)));
}

TEST(UiNewScriptModule, OneFolderTakesBothLanguagesAndIsListedOnce) {
    harness h("both_languages");
    for (const atp::studio::script_language& lang : atp::studio::languages()) {
        const auto file = create_script_module_action(h.state, h.callbacks, lang, h.dir,
                                                      std::string(lang.name_prefix) + "made", h.source_for(lang));
        ASSERT_TRUE(file.has_value()) << lang.label;
    }
    EXPECT_EQ(h.state.manager.search_dirs().size(), 1u)
        << "a folder that hosts two languages is still one plugin search directory";
}

}  // namespace
