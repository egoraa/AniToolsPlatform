// SPDX-License-Identifier: Apache-2.0
#include <filesystem>
#include <memory>

#include <gtest/gtest.h>

#include <QList>
#include <QListWidget>
#include <QString>
#include <QTreeWidget>

#include "model/app_state.hpp"
#include "panels/manager_widget.hpp"
#include "ui/qt_app.hpp"

namespace {

TEST(UiManagerWidget, OnlyAModuleThatNamesItsFileCarriesAPathToActOn) {
    (void)atp_ui_tests::ensure_app();
    atp::studio::ui::app_state state;
    state.settings_file = std::filesystem::temp_directory_path() / "atp_manager_widget_menu_tests" / "settings.json";
    state.manager.load_plugin(ATP_TEST_PLUGIN_C);
    atp::studio::ui::ui_callbacks callbacks;
    callbacks.project_changed = [] {};
    callbacks.error = [](const QString&) {};
    callbacks.selection_changed = [] {};
    callbacks.report = [](const QString&, atp::log_level) {};

    auto widget = std::make_unique<atp::studio::ui::manager_widget>(state, callbacks);
    widget->refresh();

    QTreeWidget* tree = widget->findChild<QTreeWidget*>();
    ASSERT_NE(tree, nullptr);
    ASSERT_EQ(tree->topLevelItemCount(), 1);
    QTreeWidgetItem* plugin = tree->topLevelItem(0);
    ASSERT_EQ(plugin->text(1), QStringLiteral("loaded"));
    ASSERT_GE(plugin->childCount(), 2);
    EXPECT_FALSE(plugin->data(0, Qt::UserRole).toString().isEmpty());

    EXPECT_EQ(plugin->child(0)->text(0), QStringLiteral("c_probe 2.1"));
    EXPECT_EQ(plugin->child(0)->data(0, Qt::UserRole).toString(), QStringLiteral("c_probe_declared_here.txt"));
    EXPECT_EQ(plugin->child(1)->text(0), QStringLiteral("c_bare 1"));
    EXPECT_TRUE(plugin->child(1)->data(0, Qt::UserRole).toString().isEmpty())
        << "a module compiled into its plugin has no file to open or copy";

    widget.reset();
    std::filesystem::remove_all(state.settings_file.parent_path());
}

TEST(UiManagerWidget, TheDockKeepsOneListOfDirectoriesAndDerivesTheRest) {
    (void)atp_ui_tests::ensure_app();
    atp::studio::ui::app_state state;
    state.settings_file = std::filesystem::temp_directory_path() / "atp_manager_widget_tests" / "settings.json";
    state.manager.add_search_dir("c:/plugins");
    atp::studio::ui::ui_callbacks callbacks;
    callbacks.project_changed = [] {};
    callbacks.error = [](const QString&) {};
    callbacks.selection_changed = [] {};
    callbacks.report = [](const QString&, atp::log_level) {};

    auto widget = std::make_unique<atp::studio::ui::manager_widget>(state, callbacks);
    widget->refresh();

    const QList<QListWidget*> lists = widget->findChildren<QListWidget*>();
    ASSERT_EQ(lists.size(), 1);
    ASSERT_EQ(lists.at(0)->count(), 1);
    EXPECT_EQ(lists.at(0)->item(0)->text(), QStringLiteral("c:/plugins"));

    widget.reset();
    std::filesystem::remove_all(state.settings_file.parent_path());
}

}  // namespace
