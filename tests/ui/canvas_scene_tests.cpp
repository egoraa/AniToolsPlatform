#include <filesystem>
#include <string>

#include <gtest/gtest.h>

#include <QApplication>
#include <QEvent>
#include <QGraphicsItem>
#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>
#include <QList>
#include <QPointF>

#include "canvas/canvas_items.hpp"
#include "canvas/canvas_scene.hpp"
#include "model/app_state.hpp"
#include "model/clipboard_actions.hpp"
#include "support/required.hpp"
#include "ui/qt_app.hpp"

namespace {

using atp::studio::ui::app_state;
using atp::studio::ui::canvas_scene;
using atp::studio::ui::copy_nodes;
using atp::studio::ui::node_item;
using atp::studio::ui::ui_callbacks;

QPointF node_pos(const canvas_scene& scene, const std::string& name) {
    for (QGraphicsItem* item : scene.items()) {
        if (auto* node = qgraphicsitem_cast<node_item*>(item); node != nullptr && node->child_name() == name) {
            return node->pos();
        }
    }
    return {-1.0, -1.0};
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

}  // namespace
