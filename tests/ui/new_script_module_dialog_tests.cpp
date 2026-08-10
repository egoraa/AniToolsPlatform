// SPDX-License-Identifier: Apache-2.0
#include <filesystem>
#include <fstream>
#include <memory>

#include <gtest/gtest.h>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

#include <atp/module.hpp>
#include <atp/module_registry.hpp>
#include <atp/studio/languages.hpp>

#include "shell/new_script_module_dialog.hpp"
#include "ui/qt_app.hpp"

namespace {

class taken_module : public atp::module<atp::io::ports<>, "py_taken"> {};

struct fixture {
    atp::module_registry registry;
    std::filesystem::path dir;
    std::unique_ptr<atp::studio::ui::new_script_module_dialog> dialog;
    QLineEdit* name = nullptr;
    QComboBox* language = nullptr;
    QLabel* note = nullptr;
    QPushButton* ok = nullptr;

    fixture() {
        (void)atp_ui_tests::ensure_app();
        (void)registry.add<taken_module>();
        dir = std::filesystem::temp_directory_path() / "atp_new_script_module_dialog_tests";
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        dialog = std::make_unique<atp::studio::ui::new_script_module_dialog>(QString::fromStdWString(dir.wstring()),
                                                                             registry, "python");
        name = dialog->findChild<QLineEdit*>(QStringLiteral("module_name"));
        language = dialog->findChild<QComboBox*>(QStringLiteral("language"));
        note = dialog->findChild<QLabel*>(QStringLiteral("note"));
        ok = dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
    }

    ~fixture() {
        dialog.reset();
        std::filesystem::remove_all(dir);
    }

    fixture(const fixture&) = delete;
    fixture& operator=(const fixture&) = delete;
    fixture(fixture&&) = delete;
    fixture& operator=(fixture&&) = delete;
};

TEST(UiNewScriptModuleDialog, OkWaitsForANameThatCanBecomeAModule) {
    fixture f;
    ASSERT_NE(f.name, nullptr);
    ASSERT_NE(f.ok, nullptr);

    f.name->setText(QString());
    EXPECT_FALSE(f.ok->isEnabled());
    f.name->setText(QStringLiteral("1bad"));
    EXPECT_FALSE(f.ok->isEnabled());
    f.name->setText(QStringLiteral("py_taken"));
    EXPECT_FALSE(f.ok->isEnabled());
    f.name->setText(QStringLiteral("py_fresh"));
    EXPECT_TRUE(f.ok->isEnabled());
}

TEST(UiNewScriptModuleDialog, OkRefusesANameWhoseFileIsAlreadyThere) {
    fixture f;
    std::filesystem::create_directories(f.dir / "python");
    std::ofstream(f.dir / "python" / "py_written.py") << "\n";

    f.name->setText(QStringLiteral("py_written"));
    EXPECT_FALSE(f.ok->isEnabled());

    f.name->setText(QStringLiteral("py_not_written"));
    EXPECT_TRUE(f.ok->isEnabled());
}

TEST(UiNewScriptModuleDialog, ItAnswersWithWhatWasTyped) {
    fixture f;
    f.name->setText(QStringLiteral("  py_fresh  "));

    EXPECT_EQ(f.dialog->module_name(), QStringLiteral("py_fresh"));
    EXPECT_EQ(f.dialog->directory(), QString::fromStdWString(f.dir.wstring()));
}

TEST(UiNewScriptModuleDialog, ChoosingALanguageChangesTheFileItPromises) {
    fixture f;
    ASSERT_NE(f.language, nullptr);
    ASSERT_NE(f.note, nullptr);

    f.name->setText(QStringLiteral("sample"));
    EXPECT_TRUE(f.note->text().contains(QStringLiteral("sample.py"))) << f.note->text().toStdString();

    f.language->setCurrentIndex(f.language->findData(QStringLiteral("lua")));
    EXPECT_TRUE(f.note->text().contains(QStringLiteral("sample.lua"))) << f.note->text().toStdString();
    EXPECT_EQ(f.dialog->language().id, "lua");
}

TEST(UiNewScriptModuleDialog, TheNamePrefixFollowsTheLanguageWhileItIsUntouched) {
    fixture f;
    ASSERT_NE(f.language, nullptr);
    EXPECT_EQ(f.name->text(), QStringLiteral("py_"));

    f.language->setCurrentIndex(f.language->findData(QStringLiteral("lua")));
    EXPECT_EQ(f.name->text(), QStringLiteral("lua_"));

    f.name->setText(QStringLiteral("mine"));
    f.language->setCurrentIndex(f.language->findData(QStringLiteral("python")));
    EXPECT_EQ(f.name->text(), QStringLiteral("mine")) << "a name the person typed is theirs, not the language's";
}

TEST(UiNewScriptModuleDialog, AnUnknownLanguageInTheProfileFallsBackToTheFirstOne) {
    (void)atp_ui_tests::ensure_app();
    atp::module_registry registry;
    const atp::studio::ui::new_script_module_dialog dialog(QStringLiteral("."), registry, "cobol");
    EXPECT_EQ(dialog.language().id, atp::studio::languages().front().id);
}

}  // namespace
