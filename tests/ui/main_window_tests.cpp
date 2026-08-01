#include <memory>

#include <gtest/gtest.h>

#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QList>

#include "model/app_state.hpp"
#include "shell/main_window.hpp"
#include "ui/qt_app.hpp"

namespace {

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

}  // namespace
