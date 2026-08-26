// SPDX-License-Identifier: Apache-2.0
#include <filesystem>
#include <memory>

#include <gtest/gtest.h>

#include <QLineEdit>
#include <QString>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include "model/app_state.hpp"
#include "panels/palette_widget.hpp"
#include "ui/qt_app.hpp"

namespace {

using atp::studio::ui::app_state;
using atp::studio::ui::palette_widget;
using atp::studio::ui::ui_callbacks;

ui_callbacks quiet_callbacks() {
    ui_callbacks callbacks;
    callbacks.project_changed = [] {};
    callbacks.error = [](const QString&) {};
    callbacks.selection_changed = [] {};
    callbacks.report = [](const QString&, atp::log_level) {};
    return callbacks;
}

std::unique_ptr<palette_widget> loaded_palette(app_state& state, ui_callbacks& callbacks) {
    state.manager.load_plugin(ATP_TEST_PLUGIN_C);
    auto palette = std::make_unique<palette_widget>(state, callbacks);
    palette->refresh();
    return palette;
}

int visible_modules(QTreeWidget& tree) {
    int count = 0;
    for (int i = 0; i < tree.topLevelItemCount(); ++i) {
        QTreeWidgetItem* top = tree.topLevelItem(i);
        if (top->isHidden()) {
            continue;
        }
        for (int j = 0; j < top->childCount(); ++j) {
            count += top->child(j)->isHidden() ? 0 : 1;
        }
    }
    return count;
}

int visible_plugins(QTreeWidget& tree) {
    int count = 0;
    for (int i = 0; i < tree.topLevelItemCount(); ++i) {
        QTreeWidgetItem* top = tree.topLevelItem(i);
        if (top->childCount() > 0 && !top->isHidden()) {
            ++count;
        }
    }
    return count;
}

TEST(UiPalette, TheFilterHidesWhatDoesNotMatch) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();
    auto palette = loaded_palette(state, callbacks);

    const int all = visible_modules(palette->tree());
    ASSERT_GE(all, 2);

    palette->set_filter("probe");
    EXPECT_EQ(visible_modules(palette->tree()), 1);

    palette->set_filter("");
    EXPECT_EQ(visible_modules(palette->tree()), all);
}

TEST(UiPalette, TheFilterDoesNotCareAboutCase) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();
    auto palette = loaded_palette(state, callbacks);

    palette->set_filter("PROBE");
    EXPECT_EQ(visible_modules(palette->tree()), 1);
}

TEST(UiPalette, APluginWithNoMatchingModuleGoesAwayToo) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();
    auto palette = loaded_palette(state, callbacks);

    ASSERT_GE(visible_plugins(palette->tree()), 1);

    palette->set_filter("zzzz-nothing-matches");
    EXPECT_EQ(visible_modules(palette->tree()), 0);
    EXPECT_EQ(visible_plugins(palette->tree()), 0);
}

TEST(UiPalette, TheEntryThatAddsAGroupIsNeverFilteredAway) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();
    auto palette = loaded_palette(state, callbacks);

    palette->set_filter("zzzz-nothing-matches");

    QTreeWidgetItem* first = palette->tree().topLevelItem(0);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->text(0), QStringLiteral("group"));
    EXPECT_FALSE(first->isHidden()) << "it is not a module and there is nothing about it to match";
}

TEST(UiPalette, ARescanDoesNotQuietlyWidenWhatIsOnScreen) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();
    auto palette = loaded_palette(state, callbacks);

    palette->set_filter("probe");
    ASSERT_EQ(visible_modules(palette->tree()), 1);

    palette->refresh();

    EXPECT_EQ(visible_modules(palette->tree()), 1);
}

TEST(UiPalette, AnEmptyFilterHidesNothingAtAll) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();
    auto palette = loaded_palette(state, callbacks);

    palette->set_filter("probe");
    palette->set_filter("");

    QTreeWidget& tree = palette->tree();
    for (int i = 0; i < tree.topLevelItemCount(); ++i) {
        QTreeWidgetItem* top = tree.topLevelItem(i);
        EXPECT_FALSE(top->isHidden()) << top->text(0).toStdString()
                                      << ": a plugin that registered no module would vanish the same way";
        for (int j = 0; j < top->childCount(); ++j) {
            EXPECT_FALSE(top->child(j)->isHidden()) << top->child(j)->text(0).toStdString();
        }
    }
}

TEST(UiPalette, TheFilterIsFoundByItsName) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();
    auto palette = loaded_palette(state, callbacks);

    auto* field = palette->findChild<QLineEdit*>(QStringLiteral("palette.filter"));
    ASSERT_NE(field, nullptr);
    EXPECT_FALSE(field->placeholderText().isEmpty());
}

}  // namespace
