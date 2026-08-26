// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <filesystem>
#include <ranges>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QEvent>
#include <QGraphicsItem>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QKeyEvent>
#include <QList>
#include <QPalette>
#include <QPointF>

#include "canvas/canvas_items.hpp"
#include "canvas/canvas_palette.hpp"
#include "canvas/canvas_scene.hpp"
#include "model/app_state.hpp"
#include "model/clipboard_actions.hpp"
#include "support/required.hpp"
#include "ui/qt_app.hpp"

namespace {

using atp::studio::ui::app_state;
using atp::studio::ui::canvas_colors;
using atp::studio::ui::canvas_palette;
using atp::studio::ui::canvas_scene;
using atp::studio::ui::copy_nodes;
using atp::studio::ui::node_item;
using atp::studio::ui::ui_callbacks;

ui_callbacks quiet_callbacks() {
    ui_callbacks callbacks;
    callbacks.project_changed = [] {};
    callbacks.error = [](const QString&) {};
    callbacks.selection_changed = [] {};
    return callbacks;
}

QPointF node_pos(const canvas_scene& scene, const std::string& name) {
    for (QGraphicsItem* item : scene.items()) {
        if (auto* node = qgraphicsitem_cast<node_item*>(item); node != nullptr && node->child_name() == name) {
            return node->pos();
        }
    }
    return {-1.0, -1.0};
}

QColor node_fill(const canvas_scene& scene, const std::string& name) {
    for (QGraphicsItem* item : scene.items()) {
        if (auto* node = qgraphicsitem_cast<node_item*>(item); node != nullptr && node->child_name() == name) {
            return node->brush().color();
        }
    }
    return {};
}

std::vector<QColor> node_text_colors(const canvas_scene& scene, const std::string& name) {
    std::vector<QColor> out;
    for (QGraphicsItem* item : scene.items()) {
        auto* node = qgraphicsitem_cast<node_item*>(item);
        if (node == nullptr || node->child_name() != name) {
            continue;
        }
        for (QGraphicsItem* child : node->childItems()) {
            if (auto* text = qgraphicsitem_cast<QGraphicsSimpleTextItem*>(child)) {
                out.push_back(text->brush().color());
            }
        }
    }
    return out;
}

QPalette light_scheme() {
    QPalette p;
    p.setColor(QPalette::Base, QColor(255, 255, 255));
    p.setColor(QPalette::Text, QColor(0, 0, 0));
    return p;
}

QPalette dark_scheme() {
    QPalette p;
    p.setColor(QPalette::Base, QColor(27, 27, 27));
    p.setColor(QPalette::Text, QColor(255, 255, 255));
    return p;
}

TEST(UiCanvasScene, DeletingANodeLeavesTheOthersWhereTheyWere) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks;
    callbacks.project_changed = [] {};
    callbacks.error = [](const QString&) {};
    callbacks.selection_changed = [] {};

    state.doc.add_module("", "src", "a");
    state.doc.add_module("", "mid", "b");
    state.doc.add_module("", "sink", "c");
    state.doc.connect("", "a.out", "b.in");
    state.doc.connect("", "b.out", "c.in");

    canvas_scene scene(state, callbacks);
    scene.rebuild();
    const QPointF was_a = node_pos(scene, "a");
    const QPointF was_c = node_pos(scene, "c");
    ASSERT_NE(was_a, QPointF(-1.0, -1.0));
    ASSERT_NE(was_c, QPointF(-1.0, -1.0));

    state.doc.remove_children("", {"b"});
    scene.rebuild();

    EXPECT_EQ(node_pos(scene, "a"), was_a);
    EXPECT_EQ(node_pos(scene, "c"), was_c);
}

TEST(UiCanvasScene, AFreshNodeStillGetsAnAutoLayoutSlot) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks;
    callbacks.project_changed = [] {};
    callbacks.error = [](const QString&) {};
    callbacks.selection_changed = [] {};

    state.doc.add_module("", "src", "a");
    state.doc.add_module("", "sink", "b");
    state.doc.connect("", "a.out", "b.in");

    canvas_scene scene(state, callbacks);
    scene.rebuild();

    EXPECT_NE(node_pos(scene, "a"), node_pos(scene, "b"));
    EXPECT_TRUE(state.doc.position("a").has_value());
    EXPECT_TRUE(state.doc.position("b").has_value());
}

TEST(UiCanvasScene, CopyingTakesEverySelectedNode) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks;
    callbacks.project_changed = [] {};
    callbacks.error = [](const QString&) {};
    callbacks.selection_changed = [] {};

    state.doc.add_module("", "src", "a");
    state.doc.add_module("", "mid", "b");
    state.doc.add_module("", "sink", "c");

    canvas_scene scene(state, callbacks);
    scene.rebuild();
    for (QGraphicsItem* item : scene.items()) {
        if (qgraphicsitem_cast<node_item*>(item) != nullptr) {
            item->setSelected(true);
        }
    }

    QKeyEvent copy(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier);
    QApplication::sendEvent(&scene, &copy);

    EXPECT_EQ(state.clip.nodes.size(), 3u);
}

TEST(UiCanvasScene, ARightClickOnASelectedNodeKeepsTheWholeSelection) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks;
    callbacks.project_changed = [] {};
    callbacks.error = [](const QString&) {};
    callbacks.selection_changed = [] {};

    state.doc.add_module("", "src", "a");
    state.doc.add_module("", "mid", "b");
    state.doc.add_module("", "sink", "c");

    canvas_scene scene(state, callbacks);
    scene.rebuild();
    for (QGraphicsItem* item : scene.items()) {
        if (qgraphicsitem_cast<node_item*>(item) != nullptr) {
            item->setSelected(true);
        }
    }
    ASSERT_EQ(scene.selectedItems().size(), 3);

    QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
    press.setScenePos(node_pos(scene, "a") + QPointF(10.0, 10.0));
    press.setButton(Qt::RightButton);
    press.setButtons(Qt::RightButton);
    QApplication::sendEvent(&scene, &press);

    EXPECT_EQ(scene.selectedItems().size(), 3);

    QKeyEvent copy(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier);
    QApplication::sendEvent(&scene, &copy);
    EXPECT_EQ(state.clip.nodes.size(), 3u);
}

TEST(UiCanvasScene, ARightClickOutsideTheSelectionStillMovesIt) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks;
    callbacks.project_changed = [] {};
    callbacks.error = [](const QString&) {};
    callbacks.selection_changed = [] {};

    state.doc.add_module("", "src", "a");
    state.doc.add_module("", "sink", "b");

    canvas_scene scene(state, callbacks);
    scene.rebuild();
    for (QGraphicsItem* item : scene.items()) {
        if (auto* node = qgraphicsitem_cast<node_item*>(item); node != nullptr && node->child_name() == "a") {
            node->setSelected(true);
        }
    }
    ASSERT_EQ(scene.selectedItems().size(), 1);

    QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
    press.setScenePos(node_pos(scene, "b") + QPointF(10.0, 10.0));
    press.setButton(Qt::RightButton);
    press.setButtons(Qt::RightButton);
    QApplication::sendEvent(&scene, &press);

    EXPECT_EQ(scene.selectedItems().size(), 1);
}

TEST(UiCanvasScene, PastingOnAGroupNodePutsTheCopyInsideIt) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks;
    callbacks.project_changed = [] {};
    callbacks.error = [](const QString&) {};
    callbacks.selection_changed = [] {};

    state.doc.add_module("", "src", "a");
    state.doc.add_group("", "box");

    canvas_scene scene(state, callbacks);
    scene.rebuild();
    EXPECT_TRUE(copy_nodes(state, callbacks, "", {"a"}));

    scene.paste_inside("box");

    EXPECT_EQ(state.doc.group_at("")->modules.size(), 2u);
    ASSERT_NE(state.doc.group_at("box"), nullptr);
    ASSERT_EQ(state.doc.group_at("box")->modules.size(), 1u);
    EXPECT_EQ(atp_tests::required(state.doc.group_at("box")->modules.front().module).name, "a");
}

void select_node(canvas_scene& scene, const std::string& name) {
    for (QGraphicsItem* item : scene.items()) {
        if (auto* node = qgraphicsitem_cast<node_item*>(item); node != nullptr && node->child_name() == name) {
            node->setSelected(true);
        }
    }
}

std::vector<std::string> selected_names(const canvas_scene& scene) {
    std::vector<std::string> names;
    for (QGraphicsItem* item : scene.selectedItems()) {
        if (auto* node = qgraphicsitem_cast<node_item*>(item)) {
            names.push_back(node->child_name());
        }
    }
    std::ranges::sort(names);
    return names;
}

TEST(UiCanvasScene, TheSelectedNodeIsStillSelectedAfterARebuild) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();
    state.doc.add_module("", "src", "a");
    state.doc.add_module("", "sink", "b");

    canvas_scene scene(state, callbacks);
    scene.rebuild();
    select_node(scene, "a");
    ASSERT_EQ(state.selected_child, "a");

    scene.rebuild();

    EXPECT_EQ(selected_names(scene), std::vector<std::string>{"a"});
    EXPECT_EQ(state.selected_child, "a") << "the inspector must not be left editing a node nothing selects";
}

TEST(UiCanvasScene, AWholeSelectionSurvivesARebuildAndNotJustOneOfIt) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();
    state.doc.add_module("", "src", "a");
    state.doc.add_module("", "mid", "b");
    state.doc.add_module("", "sink", "c");

    canvas_scene scene(state, callbacks);
    scene.rebuild();
    select_node(scene, "a");
    select_node(scene, "c");
    ASSERT_EQ(scene.selectedItems().size(), 2);

    scene.rebuild();

    const std::vector<std::string> expected{"a", "c"};
    EXPECT_EQ(selected_names(scene), expected) << "the project remembers one name; the scene must remember them all";
}

TEST(UiCanvasScene, ARebuildAfterTheSelectedNodeWentAwaySelectsNothing) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();
    state.doc.add_module("", "src", "a");
    state.doc.add_module("", "sink", "b");

    canvas_scene scene(state, callbacks);
    scene.rebuild();
    select_node(scene, "a");

    state.doc.remove_children("", {"a"});
    state.selected_child.clear();
    scene.rebuild();

    EXPECT_TRUE(scene.selectedItems().isEmpty());
}

TEST(UiCanvasScene, APasteStillDecidesWhatIsSelectedAfterIt) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();
    state.doc.add_module("", "src", "a");
    state.doc.add_module("", "sink", "b");

    canvas_scene scene(state, callbacks);
    scene.rebuild();
    select_node(scene, "b");
    ASSERT_TRUE(copy_nodes(state, callbacks, "", {"a"}));

    QKeyEvent paste(QEvent::KeyPress, Qt::Key_V, Qt::ControlModifier);
    QApplication::sendEvent(&scene, &paste);
    scene.rebuild();

    const std::vector<std::string> chosen = selected_names(scene);
    ASSERT_FALSE(chosen.empty()) << "a paste selects what arrived";
    EXPECT_EQ(std::ranges::count(chosen, std::string("b")), 0)
        << "and the seeding must not put the old selection back over it";
}

TEST(UiCanvasScene, SeedingTheLayoutDoesNotMarkTheProjectModified) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks;
    callbacks.project_changed = [] {};
    callbacks.error = [](const QString&) {};
    callbacks.selection_changed = [] {};

    state.doc.add_module("", "src", "a");
    state.doc.add_module("", "sink", "b");
    state.doc.save(std::filesystem::temp_directory_path() / "atp_canvas_scene_seed.json");
    ASSERT_FALSE(state.doc.is_modified());

    canvas_scene scene(state, callbacks);
    scene.rebuild();

    EXPECT_FALSE(state.doc.is_modified());
}

TEST(UiCanvasScene, TheBackgroundMenuOffersTheViewActionsTheWidgetGaveIt) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();
    canvas_scene scene(state, callbacks);

    EXPECT_FALSE(static_cast<bool>(scene.on_fit_to_window)) << "a scene with no view offers no scale to change";

    int fitted = 0;
    int reset = 0;
    scene.on_fit_to_window = [&fitted] { ++fitted; };
    scene.on_actual_size = [&reset] { ++reset; };
    EXPECT_TRUE(static_cast<bool>(scene.on_fit_to_window));
    EXPECT_TRUE(static_cast<bool>(scene.on_actual_size));

    scene.on_fit_to_window();
    scene.on_actual_size();
    EXPECT_EQ(fitted, 1);
    EXPECT_EQ(reset, 1);
}

TEST(UiCanvasScene, ALongFactoryNameIsCutToTheNodeRatherThanRunningPastIt) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();
    state.doc.add_module("", "a_factory_with_a_preposterously_long_name_that_will_not_fit", "a");

    canvas_scene scene(state, callbacks);
    scene.rebuild();

    for (QGraphicsItem* item : scene.items()) {
        auto* node = qgraphicsitem_cast<node_item*>(item);
        if (node == nullptr) {
            continue;
        }
        for (QGraphicsItem* child : node->childItems()) {
            auto* text = qgraphicsitem_cast<QGraphicsSimpleTextItem*>(child);
            if (text == nullptr) {
                continue;
            }
            EXPECT_LE(text->boundingRect().width(), atp::studio::ui::node_width)
                << text->text().toStdString() << ": a node's text must not run out past the node";
            EXPECT_FALSE(text->toolTip().isEmpty()) << "the whole of it belongs in the tooltip";
        }
    }
}

TEST(UiCanvasScene, ASchemeSwapRepaintsTheNodesAndTheirText) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks;
    callbacks.project_changed = [] {};
    callbacks.error = [](const QString&) {};
    callbacks.selection_changed = [] {};

    state.doc.add_module("", "src", "a");

    canvas_scene scene(state, callbacks);
    const canvas_palette light = canvas_colors(light_scheme());
    scene.set_colors(light);
    scene.rebuild();
    const std::vector<QColor> pale = node_text_colors(scene, "a");
    ASSERT_EQ(pale.size(), 2U);
    EXPECT_EQ(node_fill(scene, "a"), light.node_fill);
    EXPECT_EQ(pale[0], light.node_title);
    EXPECT_EQ(pale[1], light.node_alert);

    const canvas_palette dark = canvas_colors(dark_scheme());
    scene.set_colors(dark);
    scene.rebuild();
    const std::vector<QColor> deep = node_text_colors(scene, "a");
    ASSERT_EQ(deep.size(), 2U);
    EXPECT_EQ(node_fill(scene, "a"), dark.node_fill);
    EXPECT_EQ(deep[0], dark.node_title);
    EXPECT_EQ(deep[1], dark.node_alert);
    EXPECT_NE(pale[0], deep[0]);
}

TEST(UiCanvasScene, ALinkTakesTheSchemeTheSceneWasGiven) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks;
    callbacks.project_changed = [] {};
    callbacks.error = [](const QString&) {};
    callbacks.selection_changed = [] {};

    state.doc.add_module("", "src", "a");
    state.doc.add_module("", "sink", "b");
    state.doc.connect("", "a.out", "b.in");

    canvas_scene scene(state, callbacks);
    const canvas_palette light = canvas_colors(light_scheme());
    scene.set_colors(light);
    scene.rebuild();
    ASSERT_EQ(scene.links().size(), 1U);
    EXPECT_EQ(scene.links().front()->pen().color(), light.link);

    const canvas_palette dark = canvas_colors(dark_scheme());
    scene.set_colors(dark);
    scene.rebuild();
    ASSERT_EQ(scene.links().size(), 1U);
    EXPECT_EQ(scene.links().front()->pen().color(), dark.link);
}

}  // namespace
