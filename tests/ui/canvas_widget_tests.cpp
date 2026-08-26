// SPDX-License-Identifier: Apache-2.0
#include <string>

#include <gtest/gtest.h>

#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QEvent>
#include <QGraphicsItem>
#include <QGraphicsView>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QTransform>
#include <QWheelEvent>

#include "canvas/canvas_items.hpp"
#include "canvas/canvas_palette.hpp"
#include "canvas/canvas_scene.hpp"
#include "canvas/canvas_widget.hpp"
#include "model/app_state.hpp"
#include "ui/qt_app.hpp"

namespace {

using atp::studio::ui::app_state;
using atp::studio::ui::canvas_widget;
using atp::studio::ui::node_item;
using atp::studio::ui::ui_callbacks;

QColor only_node_fill(canvas_widget& canvas) {
    for (QGraphicsItem* item : canvas.scene().items()) {
        if (auto* node = qgraphicsitem_cast<node_item*>(item)) {
            return node->brush().color();
        }
    }
    return {};
}

QPalette scheme_with(const QColor& base, const QColor& text) {
    QPalette p;
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::Text, text);
    return p;
}

QPalette light_scheme() {
    return scheme_with(QColor(255, 255, 255), QColor(0, 0, 0));
}

QPalette dark_scheme() {
    return scheme_with(QColor(27, 27, 27), QColor(255, 255, 255));
}

ui_callbacks quiet_callbacks() {
    ui_callbacks callbacks;
    callbacks.project_changed = [] {};
    callbacks.error = [](const QString&) {};
    callbacks.selection_changed = [] {};
    return callbacks;
}

TEST(UiCanvasWidget, ACanvasIsBuiltInTheSchemeOfItsOwnPalette) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();
    state.doc.add_module("", "src", "a");

    canvas_widget canvas(state, callbacks);
    canvas.setPalette(dark_scheme());
    canvas.refresh();

    EXPECT_LT(only_node_fill(canvas).lightness(), 128);
}

TEST(UiCanvasWidget, ChangingTheWidgetPaletteRepaintsTheCanvas) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();
    state.doc.add_module("", "src", "a");

    canvas_widget canvas(state, callbacks);
    canvas.setPalette(light_scheme());
    canvas.refresh();
    const QColor was = only_node_fill(canvas);
    ASSERT_GT(was.lightness(), 128);

    canvas.setPalette(dark_scheme());

    const QColor now = only_node_fill(canvas);
    EXPECT_LT(now.lightness(), 128);
    EXPECT_NE(was, now);
}

TEST(UiCanvasWidget, RefreshingPicksUpASchemeNoEventAnnounced) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();
    state.doc.add_module("", "src", "a");

    canvas_widget canvas(state, callbacks);
    canvas.setPalette(light_scheme());
    canvas.refresh();
    ASSERT_GT(only_node_fill(canvas).lightness(), 128);

    canvas.scene().set_colors(atp::studio::ui::canvas_colors(dark_scheme()));
    canvas.scene().rebuild();
    ASSERT_LT(only_node_fill(canvas).lightness(), 128);

    canvas.refresh();

    EXPECT_GT(only_node_fill(canvas).lightness(), 128);
}

TEST(UiCanvasWidget, ZoomingInAndOutRetracesItsOwnSteps) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();
    state.doc.add_module("", "src", "a");

    canvas_widget canvas(state, callbacks);
    canvas.refresh();
    EXPECT_DOUBLE_EQ(canvas.zoom(), 1.0);

    canvas.zoom_in();
    const double in = canvas.zoom();
    EXPECT_GT(in, 1.0);
    canvas.zoom_out();
    EXPECT_NEAR(canvas.zoom(), 1.0, 1e-9);

    canvas.zoom_in();
    canvas.zoom_in();
    canvas.zoom_reset();
    EXPECT_DOUBLE_EQ(canvas.zoom(), 1.0);
}

TEST(UiCanvasWidget, ZoomStopsAtItsBoundsRatherThanRunningAway) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();
    canvas_widget canvas(state, callbacks);
    canvas.refresh();

    for (int i = 0; i < 100; ++i) {
        canvas.zoom_in();
    }
    EXPECT_LE(canvas.zoom(), 8.0);
    EXPECT_GT(canvas.zoom(), 1.0);

    for (int i = 0; i < 200; ++i) {
        canvas.zoom_out();
    }
    EXPECT_GE(canvas.zoom(), 0.125);
    EXPECT_LT(canvas.zoom(), 1.0);
}

TEST(UiCanvasWidget, TheViewFollowsTheZoomItReports) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();
    state.doc.add_module("", "src", "a");

    canvas_widget canvas(state, callbacks);
    canvas.refresh();
    canvas.zoom_in();

    auto* view = canvas.findChild<QGraphicsView*>();
    ASSERT_NE(view, nullptr);
    EXPECT_NEAR(view->transform().m11(), canvas.zoom(), 1e-9);
    EXPECT_NEAR(view->transform().m22(), canvas.zoom(), 1e-9);
}

TEST(UiCanvasWidget, FittingAnEmptyCanvasLeavesTheScaleAlone) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();
    canvas_widget canvas(state, callbacks);
    canvas.refresh();

    canvas.zoom_to_fit();

    EXPECT_DOUBLE_EQ(canvas.zoom(), 1.0);
}

TEST(UiCanvasWidget, FittingAFullCanvasKeepsTheScaleInRange) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();
    for (int i = 0; i < 12; ++i) {
        state.doc.add_module("", "src", "m" + std::to_string(i));
    }

    canvas_widget canvas(state, callbacks);
    canvas.resize(400, 300);
    canvas.refresh();
    canvas.zoom_to_fit();

    EXPECT_GE(canvas.zoom(), 0.125);
    EXPECT_LE(canvas.zoom(), 8.0);
}

TEST(UiCanvasWidget, TheWheelZoomsOnlyWithControl) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();
    state.doc.add_module("", "src", "a");

    canvas_widget canvas(state, callbacks);
    canvas.refresh();
    auto* view = canvas.findChild<QGraphicsView*>();
    ASSERT_NE(view, nullptr);

    const auto turn = [&](Qt::KeyboardModifiers mods, int notch) {
        QWheelEvent wheel(QPointF(10.0, 10.0), QPointF(10.0, 10.0), QPoint(), QPoint(0, notch), Qt::NoButton, mods,
                          Qt::NoScrollPhase, false);
        QApplication::sendEvent(view->viewport(), &wheel);
    };

    turn(Qt::NoModifier, 120);
    EXPECT_DOUBLE_EQ(canvas.zoom(), 1.0) << "the wheel alone scrolls, as it does in any other scroll area";

    turn(Qt::ControlModifier, 120);
    EXPECT_GT(canvas.zoom(), 1.0);

    turn(Qt::ControlModifier, -120);
    EXPECT_NEAR(canvas.zoom(), 1.0, 1e-9);
}

TEST(UiCanvasWidget, ASidewaysWheelIsPassedOnRatherThanEaten) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();
    canvas_widget canvas(state, callbacks);
    canvas.refresh();
    auto* view = canvas.findChild<QGraphicsView*>();
    ASSERT_NE(view, nullptr);

    QWheelEvent sideways(QPointF(10.0, 10.0), QPointF(10.0, 10.0), QPoint(), QPoint(120, 0), Qt::NoButton,
                         Qt::ControlModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(view->viewport(), &sideways);

    EXPECT_DOUBLE_EQ(canvas.zoom(), 1.0);
}

TEST(UiCanvasWidget, ACrumbIsTheNameAloneAndTheSlashIsBesideIt) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();
    state.doc.add_group("", "producers");
    state.current_group = "producers";

    canvas_widget canvas(state, callbacks);
    canvas.refresh();

    bool found = false;
    for (QPushButton* crumb : canvas.findChildren<QPushButton*>()) {
        EXPECT_FALSE(crumb->text().contains(QLatin1Char('/')))
            << crumb->text().toStdString() << ": the separator is not part of what is clicked";
        if (crumb->text() == QStringLiteral("producers")) {
            found = true;
        }
    }
    EXPECT_TRUE(found);

    bool separated = false;
    for (QLabel* mark : canvas.findChildren<QLabel*>()) {
        if (mark->text() == QStringLiteral("/")) {
            separated = true;
        }
    }
    EXPECT_TRUE(separated);
}

TEST(UiCanvasWidget, TheCanvasMenuCanFitThePipelineAndPutTheScaleBack) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();
    for (int i = 0; i < 12; ++i) {
        state.doc.add_module("", "src", "m" + std::to_string(i));
    }

    canvas_widget canvas(state, callbacks);
    canvas.resize(400, 300);
    canvas.refresh();

    ASSERT_TRUE(static_cast<bool>(canvas.scene().on_fit_to_window))
        << "the widget owns the scale, so it supplies these";
    ASSERT_TRUE(static_cast<bool>(canvas.scene().on_actual_size));

    canvas.scene().on_fit_to_window();
    EXPECT_NE(canvas.zoom(), 1.0);

    canvas.scene().on_actual_size();
    EXPECT_DOUBLE_EQ(canvas.zoom(), 1.0);
}

TEST(UiCanvasWidget, AnEventThatIsNotASchemeChangeLeavesTheCanvasAlone) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();
    state.doc.add_module("", "src", "a");

    canvas_widget canvas(state, callbacks);
    canvas.setPalette(dark_scheme());
    canvas.refresh();
    const QColor was = only_node_fill(canvas);

    QEvent unrelated(QEvent::FontChange);
    QApplication::sendEvent(&canvas, &unrelated);

    EXPECT_EQ(only_node_fill(canvas), was);
}

}  // namespace
