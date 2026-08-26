// SPDX-License-Identifier: Apache-2.0
#include <memory>
#include <utility>

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QDockWidget>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLabel>
#include <QList>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QStringList>
#include <QTabWidget>
#include <QToolBar>

#include "model/app_state.hpp"
#include "shell/main_window.hpp"
#include "ui/qt_app.hpp"

namespace {

using atp::studio::ui::app_state;
using atp::studio::ui::main_window;

TEST(UiMainWindow, ClosingWithASelectedNodeDoesNotReachTheDeadInspector) {
    (void)atp_ui_tests::ensure_app();
    atp::studio::ui::app_state state;
    state.doc.add_module("", "src", "a");

    auto window = std::make_unique<atp::studio::ui::main_window>(state);
    QGraphicsView* view = window->findChild<QGraphicsView*>();
    ASSERT_NE(view, nullptr);
    ASSERT_NE(view->scene(), nullptr);
    const QList<QGraphicsItem*> items = view->scene()->items();
    ASSERT_FALSE(items.isEmpty());
    for (QGraphicsItem* item : items) {
        item->setSelected(true);
    }
    ASSERT_EQ(state.selected_child, "a");

    window.reset();
}

TEST(UiMainWindow, TheToolbarCarriesTheActionsAPersonReachesFor) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    auto window = std::make_unique<main_window>(state);

    auto* bar = window->findChild<QToolBar*>(QStringLiteral("toolbar.main"));
    ASSERT_NE(bar, nullptr);
    QStringList names;
    for (QAction* action : bar->actions()) {
        if (!action->isSeparator()) {
            names << action->objectName();
        }
    }
    for (const QString& wanted : {"action.new", "action.open", "action.save", "action.undo", "action.redo",
                                  "action.new_group", "action.run", "action.stop"}) {
        EXPECT_TRUE(names.contains(wanted)) << wanted.toStdString();
    }
}

TEST(UiMainWindow, TheToolbarGroupsAreDividedByPlainSeparators) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    auto window = std::make_unique<main_window>(state);

    auto* bar = window->findChild<QToolBar*>(QStringLiteral("toolbar.main"));
    ASSERT_NE(bar, nullptr);

    int separators = 0;
    int nameless = 0;
    for (QAction* action : bar->actions()) {
        if (action->isSeparator()) {
            ++separators;
        } else if (action->objectName().isEmpty()) {
            ++nameless;
        }
    }
    EXPECT_EQ(separators, 3) << "file, edit, structure, transport";
    EXPECT_EQ(nameless, 0) << "the style decides what a separator looks like; padding it puts the groups too far apart";
}

TEST(UiMainWindow, EveryToolbarActionCarriesAnIcon) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    auto window = std::make_unique<main_window>(state);

    auto* bar = window->findChild<QToolBar*>(QStringLiteral("toolbar.main"));
    ASSERT_NE(bar, nullptr);
    for (QAction* action : bar->actions()) {
        if (!action->isSeparator()) {
            EXPECT_FALSE(action->icon().isNull()) << action->objectName().toStdString();
        }
    }
}

TEST(UiMainWindow, TheStatusBarSaysWhatThePipelineIsDoingAndWhichFileIsOpen) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    auto window = std::make_unique<main_window>(state);

    auto* run = window->findChild<QLabel*>(QStringLiteral("status.run"));
    auto* path = window->findChild<QLabel*>(QStringLiteral("status.path"));
    ASSERT_NE(run, nullptr);
    ASSERT_NE(path, nullptr);
    EXPECT_EQ(run->text(), QStringLiteral("stopped"));
    EXPECT_EQ(path->text(), QStringLiteral("Untitled"));
}

TEST(UiMainWindow, StopIsOffWhileNothingRuns) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    auto window = std::make_unique<main_window>(state);

    auto* run = window->findChild<QAction*>(QStringLiteral("action.run"));
    auto* stop = window->findChild<QAction*>(QStringLiteral("action.stop"));
    ASSERT_NE(run, nullptr);
    ASSERT_NE(stop, nullptr);
    EXPECT_TRUE(run->isEnabled());
    EXPECT_FALSE(stop->isEnabled());
}

/// The studio's own dock set, so the blob under test is the shape a real profile holds rather than a
/// simplified one.
void add_studio_docks(QMainWindow& window) {
    QDockWidget* first = nullptr;
    for (const auto& [name, area] :
         {std::pair{"dock.project", Qt::LeftDockWidgetArea}, std::pair{"dock.palette", Qt::LeftDockWidgetArea},
          std::pair{"dock.inspector", Qt::RightDockWidgetArea}, std::pair{"dock.log", Qt::BottomDockWidgetArea},
          std::pair{"dock.plugins", Qt::BottomDockWidgetArea}, std::pair{"dock.runtime", Qt::BottomDockWidgetArea}}) {
        auto* dock = new QDockWidget(QString::fromLatin1(name), &window);
        dock->setObjectName(QString::fromLatin1(name));
        window.addDockWidget(area, dock);
        if (QString::fromLatin1(name) == QStringLiteral("dock.log")) {
            first = dock;
        } else if (first != nullptr && QString::fromLatin1(name) == QStringLiteral("dock.plugins")) {
            window.tabifyDockWidget(first, dock);
        }
    }
}

TEST(UiMainWindow, AProfileSavedBeforeTheToolbarExistedDoesNotSwallowIt) {
    (void)atp_ui_tests::ensure_app();

    QMainWindow before;
    add_studio_docks(before);
    const QByteArray blob = before.saveState(2);

    QMainWindow after;
    add_studio_docks(after);
    auto* bar = after.addToolBar("Main");
    bar->setObjectName(QStringLiteral("toolbar.main"));

    EXPECT_TRUE(after.restoreState(blob, 2)) << "the blob is the same version, so it is accepted";
    EXPECT_FALSE(bar->isHidden()) << "a toolbar the stream never mentions keeps its default place";
    EXPECT_TRUE(bar->isVisibleTo(&after));
}

TEST(UiMainWindow, TheStyleMenuOffersTwoLooksRatherThanEverythingQtCanBuild) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    auto window = std::make_unique<main_window>(state);

    QMenu* view = nullptr;
    for (QAction* action : window->menuBar()->actions()) {
        if (action->text() == QStringLiteral("&View")) {
            view = action->menu();
        }
    }
    ASSERT_NE(view, nullptr);
    QMenu* style = nullptr;
    for (QAction* action : view->actions()) {
        if (action->text() == QStringLiteral("St&yle")) {
            style = action->menu();
        }
    }
    ASSERT_NE(style, nullptr);

    int entries = 0;
    for (QAction* action : style->actions()) {
        if (!action->isSeparator()) {
            ++entries;
        }
    }
    EXPECT_EQ(entries, 2) << "System and Fusion; the rest render the panels in a look they were not drawn for";
}

TEST(UiMainWindow, TheDefaultLayoutLeavesTheCanvasMoreRoomThanTheBottomDocks) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    auto window = std::make_unique<main_window>(state);
    window->resize(1600, 900);
    window->show();
    QApplication::processEvents();
    window->reset_layout();
    QApplication::processEvents();

    auto* runtime = window->findChild<QDockWidget*>(QStringLiteral("dock.runtime"));
    auto* canvas = window->centralWidget();
    ASSERT_NE(runtime, nullptr);
    ASSERT_NE(canvas, nullptr);
    EXPECT_GT(canvas->height(), runtime->height()) << "canvas " << canvas->height() << " runtime " << runtime->height();
    EXPECT_LT(runtime->height(), window->height() / 2);
}

TEST(UiMainWindow, TheBottomRowKeepsItsShareOfATallerWindow) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    auto window = std::make_unique<main_window>(state);
    window->resize(1600, 1800);
    window->show();
    QApplication::processEvents();
    window->reset_layout();
    QApplication::processEvents();

    auto* runtime = window->findChild<QDockWidget*>(QStringLiteral("dock.runtime"));
    ASSERT_NE(runtime, nullptr);
    EXPECT_LT(runtime->height(), window->height() / 2);
    EXPECT_GT(runtime->height(), window->height() / 6)
        << "a fixed pixel height would shrink to nothing on a tall window";
}

TEST(UiMainWindow, TheTabsOfADockSitAboveItsContent) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    auto window = std::make_unique<main_window>(state);
    for (const Qt::DockWidgetArea area :
         {Qt::LeftDockWidgetArea, Qt::RightDockWidgetArea, Qt::TopDockWidgetArea, Qt::BottomDockWidgetArea}) {
        EXPECT_EQ(window->tabPosition(area), QTabWidget::North) << "area " << static_cast<int>(area);
    }
}

}  // namespace
