// SPDX-License-Identifier: Apache-2.0
#include <string>

#include <gtest/gtest.h>

#include <QApplication>
#include <QClipboard>
#include <QString>

#include <atp/hosting/module_registry.hpp>
#include <atp/module.hpp>

#include "kit/property_grid.hpp"
#include "model/app_state.hpp"
#include "ui/qt_app.hpp"

namespace {

using atp::studio::ui::app_state;
using atp::studio::ui::property_grid;
using atp::studio::ui::ui_callbacks;

struct knob_props : atp::io::properties {
    atp::io::property<int>& step = make<atp::io::property<int>>("step", 1);
    atp::io::property<bool>& loud = make<atp::io::property<bool>>("loud", false);
};
class knob_module : public atp::module<atp::ports<atp::io::inputs, atp::io::outputs, knob_props>, "knob"> {};

struct dial_props : atp::io::properties {
    atp::io::property<std::string>& mode =
        make<atp::io::property<std::string>>("mode", "fast", atp::io::allowed("fast", "slow"));
    atp::io::property<int>& gain = make<atp::io::property<int>>("gain", 1);
};
class dial_module : public atp::module<atp::ports<atp::io::inputs, atp::io::outputs, dial_props>, "dial"> {};

struct harness {
    app_state state;
    ui_callbacks callbacks;

    harness() {
        callbacks.project_changed = [] {};
        callbacks.error = [](const QString&) {};
        callbacks.selection_changed = [] {};
        state.manager.registry().add<knob_module>();
        state.manager.registry().add<dial_module>();
        state.doc.add_module("", "knob", "a");
        state.doc.add_module("", "dial", "d");
    }

    void fill(property_grid& grid, const std::string& name = "a") {
        for (const atp::runtime::child_node& c : state.doc.group_at("")->modules) {
            if (c.module && c.module->name == name) {
                grid.rebuild("", name, *c.module);
                return;
            }
        }
    }
};

TEST(UiPropertyGrid, ValueTextShowsWhatTheRowShows) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.state.doc.set_property("", "a", "step", 7);

    property_grid grid(h.state, h.callbacks);
    h.fill(grid);

    EXPECT_EQ(grid.value_text("step"), QStringLiteral("7"));
    EXPECT_EQ(grid.value_text("loud"), QStringLiteral("false"));
    EXPECT_TRUE(grid.value_text("ghost").isEmpty());
}

TEST(UiPropertyGrid, AllTextIsOneNameAndValuePerLineSortedByName) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.state.doc.set_property("", "a", "step", 7);

    property_grid grid(h.state, h.callbacks);
    h.fill(grid);

    EXPECT_EQ(grid.all_text(), QStringLiteral("loud = false\nstep = 7"));
}

TEST(UiPropertyGrid, CopyAllPutsTheLinesOnTheClipboard) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    property_grid grid(h.state, h.callbacks);
    h.fill(grid);
    QApplication::clipboard()->setText(QStringLiteral("something else"));

    grid.copy_all();

    EXPECT_EQ(QApplication::clipboard()->text(), QStringLiteral("loud = false\nstep = 1"));
}

TEST(UiPropertyGrid, CopyValuePutsJustTheValueOnTheClipboard) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.state.doc.set_property("", "a", "step", 7);
    property_grid grid(h.state, h.callbacks);
    h.fill(grid);

    grid.copy_value("step");

    EXPECT_EQ(QApplication::clipboard()->text(), QStringLiteral("7"));
}

TEST(UiPropertyGrid, CopyingAnUnknownRowLeavesTheClipboardAlone) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    property_grid grid(h.state, h.callbacks);
    h.fill(grid);
    QApplication::clipboard()->setText(QStringLiteral("kept"));

    grid.copy_value("ghost");

    EXPECT_EQ(QApplication::clipboard()->text(), QStringLiteral("kept"));
}

TEST(UiPropertyGrid, TheEditorsLeaveTheContextMenuToTheGrid) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    property_grid grid(h.state, h.callbacks);
    h.fill(grid, "d");

    const QList<QLineEdit*> lines = grid.findChildren<QLineEdit*>();
    const QList<QComboBox*> combos = grid.findChildren<QComboBox*>();
    ASSERT_FALSE(lines.isEmpty());
    ASSERT_FALSE(combos.isEmpty());
    for (const QLineEdit* w : lines) {
        EXPECT_EQ(w->contextMenuPolicy(), Qt::NoContextMenu);
    }
    for (const QComboBox* w : combos) {
        EXPECT_EQ(w->contextMenuPolicy(), Qt::NoContextMenu);
    }
}

TEST(UiPropertyGrid, AGridWithNoRowsHasNothingToCopy) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    property_grid grid(h.state, h.callbacks);
    QApplication::clipboard()->setText(QStringLiteral("kept"));

    EXPECT_TRUE(grid.empty());
    EXPECT_TRUE(grid.all_text().isEmpty());
    grid.copy_all();

    EXPECT_EQ(QApplication::clipboard()->text(), QStringLiteral("kept"));
}

}  // namespace
