// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <ranges>
#include <string>
#include <typeindex>
#include <vector>

#include <gtest/gtest.h>

#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QEvent>
#include <QGraphicsItem>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QImage>
#include <QKeyEvent>
#include <QList>
#include <QPainter>
#include <QPalette>
#include <QPointF>
#include <QRectF>

#include <atp/studio/languages.hpp>
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
using atp::studio::ui::glyph_item;
using atp::studio::ui::node_item;
using atp::studio::ui::stub_item;
using atp::studio::ui::ui_callbacks;

/// A pipeline that runs and reports whatever the test put in `samples`, so the monitoring overlay can
/// be polled without a live runner.
class running_view final : public atp::studio::runtime_view_base {
   public:
    std::vector<atp::runtime::connection_sample> samples;

    [[nodiscard]] bool running() const override {
        return true;
    }
    [[nodiscard]] std::string error_text() const override {
        return {};
    }
    [[nodiscard]] std::vector<atp::runtime::pipeline_runner::thread_stats> stats() const override {
        return {};
    }
    [[nodiscard]] std::vector<atp::runtime::connection_sample> sample_connections() const override {
        return samples;
    }
    [[nodiscard]] std::vector<atp::runtime::group::module_stats> module_metrics() const override {
        return {};
    }
    [[nodiscard]] std::vector<atp::runtime::group::port_stats> input_metrics() const override {
        return {};
    }
    [[nodiscard]] bool metrics_enabled() const override {
        return false;
    }
    bool set_metrics_enabled(bool) override {
        return false;
    }
    [[nodiscard]] std::vector<atp::studio::live_property> live_properties(const std::string&) const override {
        return {};
    }
    void set_property(const atp::runtime::property_override&) override {}
};

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

glyph_item* node_glyph(const canvas_scene& scene, const std::string& name) {
    for (QGraphicsItem* item : scene.items()) {
        auto* node = qgraphicsitem_cast<node_item*>(item);
        if (node == nullptr || node->child_name() != name) {
            continue;
        }
        for (QGraphicsItem* child : node->childItems()) {
            if (auto* glyph = qgraphicsitem_cast<glyph_item*>(child)) {
                return glyph;
            }
        }
    }
    return nullptr;
}

QGraphicsSimpleTextItem* node_title(const canvas_scene& scene, const std::string& name) {
    for (QGraphicsItem* item : scene.items()) {
        auto* node = qgraphicsitem_cast<node_item*>(item);
        if (node == nullptr || node->child_name() != name) {
            continue;
        }
        for (QGraphicsItem* child : node->childItems()) {
            if (auto* text = qgraphicsitem_cast<QGraphicsSimpleTextItem*>(child)) {
                return text;
            }
        }
    }
    return nullptr;
}

node_item* node_by_name(const canvas_scene& scene, const std::string& name) {
    for (QGraphicsItem* item : scene.items()) {
        if (auto* node = qgraphicsitem_cast<node_item*>(item); node != nullptr && node->child_name() == name) {
            return node;
        }
    }
    return nullptr;
}

std::vector<std::string> stacking(const canvas_scene& scene) {
    std::vector<std::string> out;
    for (QGraphicsItem* item : scene.items()) {
        if (auto* node = qgraphicsitem_cast<node_item*>(item)) {
            out.push_back(node->child_name());
        }
    }
    return out;
}

void drag_node(canvas_scene& scene, QPointF from, QPointF to) {
    QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
    press.setScenePos(from);
    press.setButton(Qt::LeftButton);
    press.setButtons(Qt::LeftButton);
    QApplication::sendEvent(&scene, &press);

    QGraphicsSceneMouseEvent move(QEvent::GraphicsSceneMouseMove);
    move.setScenePos(to);
    move.setButtons(Qt::LeftButton);
    QApplication::sendEvent(&scene, &move);

    QGraphicsSceneMouseEvent release(QEvent::GraphicsSceneMouseRelease);
    release.setScenePos(to);
    release.setButton(Qt::LeftButton);
    QApplication::sendEvent(&scene, &release);
}

void ctrl_click(canvas_scene& scene, QPointF at) {
    QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
    press.setScenePos(at);
    press.setButton(Qt::LeftButton);
    press.setButtons(Qt::LeftButton);
    press.setModifiers(Qt::ControlModifier);
    QApplication::sendEvent(&scene, &press);

    QGraphicsSceneMouseEvent release(QEvent::GraphicsSceneMouseRelease);
    release.setScenePos(at);
    release.setButton(Qt::LeftButton);
    release.setModifiers(Qt::ControlModifier);
    QApplication::sendEvent(&scene, &release);
}

atp::studio::module_info ported(const std::string& factory,
                                const std::vector<std::string>& inputs,
                                const std::vector<std::string>& outputs) {
    atp::studio::module_info info;
    info.name = factory;
    for (const std::string& port : inputs) {
        info.inputs.push_back({port, std::type_index(typeid(int))});
    }
    for (const std::string& port : outputs) {
        info.outputs.push_back({port, std::type_index(typeid(int))});
    }
    return info;
}

stub_item* stub_by_alias(const canvas_scene& scene, const std::string& alias) {
    for (QGraphicsItem* item : scene.items()) {
        if (auto* stub = qgraphicsitem_cast<stub_item*>(item); stub != nullptr && stub->alias() == alias) {
            return stub;
        }
    }
    return nullptr;
}

atp::studio::module_info described_as(const std::string& factory, const std::string& source) {
    atp::studio::module_info info;
    info.name = factory;
    info.source = source;
    return info;
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

TEST(UiCanvasScene, AStubLightsFromTheConnectionOutsideTheGroup) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();

    state.doc.add_module("", "src", "a");
    state.doc.add_group("", "g");
    state.doc.add_module("g", "sink", "b");
    state.doc.set_expose_input("g", "in", "b.in");
    state.doc.connect("", "a.out", "g.in");
    state.describe_cache.emplace("src@latest", ported("src", {}, {"out"}));
    state.describe_cache.emplace("sink@latest", ported("sink", {"in"}, {}));

    running_view view;
    state.view = &view;
    state.current_group = "g";

    canvas_scene scene(state, callbacks);
    scene.rebuild();
    stub_item* stub = stub_by_alias(scene, "in");
    ASSERT_NE(stub, nullptr);
    const QColor idle = stub->pen().color();

    view.samples = {{"", 0, 7}};
    scene.update_samples();
    EXPECT_EQ(stub->pen().color(), idle) << "a first reading is a count, not growth: nothing was seen to arrive";

    view.samples = {{"", 0, 8}};
    scene.update_samples();
    EXPECT_NE(stub->pen().color(), idle) << "what travels an exported port is only counted one level up";

    scene.update_samples();
    EXPECT_EQ(stub->pen().color(), idle) << "nothing arrived since the last poll";
}

TEST(UiCanvasScene, AStubOfAPortExportedAgainLooksFurtherOut) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();

    state.doc.add_module("", "src", "a");
    state.doc.add_group("", "outer");
    state.doc.add_group("outer", "inner");
    state.doc.add_module("outer.inner", "sink", "b");
    state.doc.set_expose_input("outer.inner", "in", "b.in");
    state.doc.set_expose_input("outer", "x", "inner.in");
    state.doc.connect("", "a.out", "outer.x");
    state.describe_cache.emplace("src@latest", ported("src", {}, {"out"}));
    state.describe_cache.emplace("sink@latest", ported("sink", {"in"}, {}));

    running_view view;
    view.samples = {{"", 0, 3}};
    state.view = &view;
    state.current_group = "outer.inner";

    canvas_scene scene(state, callbacks);
    scene.rebuild();
    stub_item* stub = stub_by_alias(scene, "in");
    ASSERT_NE(stub, nullptr);
    const QColor idle = stub->pen().color();

    scene.update_samples();
    view.samples = {{"", 0, 4}};
    scene.update_samples();
    EXPECT_NE(stub->pen().color(), idle) << "a port exported again is carried by a connection further out still";
}

TEST(UiCanvasScene, AStubNothingConnectsToStaysCold) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();

    state.doc.add_module("", "src", "a");
    state.doc.add_group("", "g");
    state.doc.add_module("g", "sink", "b");
    state.doc.set_expose_input("g", "in", "b.in");
    state.doc.add_module("", "sink", "c");
    state.doc.connect("", "a.out", "c.in");
    state.describe_cache.emplace("src@latest", ported("src", {}, {"out"}));
    state.describe_cache.emplace("sink@latest", ported("sink", {"in"}, {}));

    running_view view;
    view.samples = {{"", 0, 9}};
    state.view = &view;
    state.current_group = "g";

    canvas_scene scene(state, callbacks);
    scene.rebuild();
    stub_item* stub = stub_by_alias(scene, "in");
    ASSERT_NE(stub, nullptr);
    const QColor idle = stub->pen().color();

    scene.update_samples();
    view.samples = {{"", 0, 10}};
    scene.update_samples();
    EXPECT_EQ(stub->pen().color(), idle) << "the busy connection out there does not lead to this port";
}

TEST(UiCanvasScene, AStubLightsFromWhicheverConnectionFeedsIt) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();

    state.doc.add_module("", "src", "a");
    state.doc.add_module("", "src", "d");
    state.doc.add_group("", "g");
    state.doc.add_module("g", "sink", "b");
    state.doc.set_expose_input("g", "in", "b.in");
    state.doc.connect("", "a.out", "g.in");
    state.doc.connect("", "d.out", "g.in");
    state.describe_cache.emplace("src@latest", ported("src", {}, {"out"}));
    state.describe_cache.emplace("sink@latest", ported("sink", {"in"}, {}));

    running_view view;
    view.samples = {{"", 0, 5}, {"", 1, 5}};
    state.view = &view;
    state.current_group = "g";

    canvas_scene scene(state, callbacks);
    scene.rebuild();
    stub_item* stub = stub_by_alias(scene, "in");
    ASSERT_NE(stub, nullptr);
    const QColor idle = stub->pen().color();

    scene.update_samples();
    view.samples = {{"", 0, 5}, {"", 1, 6}};
    scene.update_samples();
    EXPECT_NE(stub->pen().color(), idle) << "one idle writer must not hide a busy one feeding the same port";
}

TEST(UiCanvasScene, EnteringARunningPipelineLightsNothingUntilSomethingArrives) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();

    state.doc.add_module("", "src", "a");
    state.doc.add_module("", "sink", "b");
    state.doc.connect("", "a.out", "b.in");
    state.describe_cache.emplace("src@latest", ported("src", {}, {"out"}));
    state.describe_cache.emplace("sink@latest", ported("sink", {"in"}, {}));

    running_view view;
    view.samples = {{"", 0, 1000}};
    state.view = &view;

    canvas_scene scene(state, callbacks);
    scene.rebuild();
    ASSERT_EQ(scene.links().size(), 1u);
    const QColor idle = scene.links().front()->pen().color();

    scene.update_samples();
    EXPECT_EQ(scene.links().front()->pen().color(), idle)
        << "a pipeline that has run for a while and stopped delivering must not flash on the first poll";

    view.samples = {{"", 0, 1001}};
    scene.update_samples();
    EXPECT_NE(scene.links().front()->pen().color(), idle);
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

TEST(UiCanvasScene, AModuleFromAPluginWearsTheBinaryMark) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();

    state.doc.add_module("", "src", "a");
    state.describe_cache.emplace("src@latest", described_as("src", ""));

    canvas_scene scene(state, callbacks);
    scene.rebuild();

    glyph_item* glyph = node_glyph(scene, "a");
    ASSERT_NE(glyph, nullptr);
    EXPECT_EQ(glyph->artwork(), QStringLiteral(":/icons/module.svg"));
    EXPECT_TRUE(glyph->valid());
    EXPECT_EQ(glyph->toolTip(), QStringLiteral("Binary module"));
}

TEST(UiCanvasScene, AModuleDeclaredInAScriptWearsTheMarkOfItsLanguage) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();

    for (const atp::studio::script_language& lang : atp::studio::languages()) {
        const std::string factory = std::string("mod_") + std::string(lang.id);
        const std::string source = std::string("/home/user/modules/my_filter") + std::string(lang.file_extension);
        state.doc.add_module("", factory, factory);
        state.describe_cache.emplace(factory + "@latest", described_as(factory, source));
    }

    canvas_scene scene(state, callbacks);
    scene.rebuild();

    for (const atp::studio::script_language& lang : atp::studio::languages()) {
        const std::string factory = std::string("mod_") + std::string(lang.id);
        glyph_item* glyph = node_glyph(scene, factory);
        ASSERT_NE(glyph, nullptr) << factory;
        EXPECT_EQ(glyph->artwork(), QStringLiteral(":/icons/") +
                                        QString::fromUtf8(lang.id.data(), static_cast<qsizetype>(lang.id.size())) +
                                        QStringLiteral(".svg"));
        EXPECT_TRUE(glyph->valid()) << factory;
        EXPECT_TRUE(glyph->toolTip().startsWith(
            QString::fromUtf8(lang.label.data(), static_cast<qsizetype>(lang.label.size()))));
        EXPECT_TRUE(glyph->toolTip().contains(QStringLiteral("my_filter")));
    }
}

TEST(UiCanvasScene, AScriptOfALanguageNothingHereKnowsIsStillAScript) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();

    state.doc.add_module("", "exotic", "a");
    state.describe_cache.emplace("exotic@latest", described_as("exotic", "/home/user/modules/exotic.rb"));

    canvas_scene scene(state, callbacks);
    scene.rebuild();

    glyph_item* glyph = node_glyph(scene, "a");
    ASSERT_NE(glyph, nullptr);
    EXPECT_EQ(glyph->artwork(), QStringLiteral(":/icons/script.svg"));
    EXPECT_TRUE(glyph->valid());
    EXPECT_TRUE(glyph->toolTip().contains(QStringLiteral("exotic.rb")));
    EXPECT_NE(glyph->toolTip(), QStringLiteral("Binary module"));
}

TEST(UiCanvasScene, ASubgroupWearsTheFolderTheProjectTreeGivesIt) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();

    state.doc.add_group("", "inner");

    canvas_scene scene(state, callbacks);
    scene.rebuild();

    glyph_item* glyph = node_glyph(scene, "inner");
    ASSERT_NE(glyph, nullptr);
    EXPECT_EQ(glyph->artwork(), QStringLiteral(":/icons/group.svg"));
    EXPECT_TRUE(glyph->valid());
}

TEST(UiCanvasScene, AModuleWhoseFactoryIsMissingSaysSoRatherThanClaimingToBeBinary) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();

    state.doc.add_module("", "gone", "a");

    canvas_scene scene(state, callbacks);
    scene.rebuild();

    glyph_item* glyph = node_glyph(scene, "a");
    ASSERT_NE(glyph, nullptr);
    EXPECT_EQ(glyph->artwork(), QStringLiteral(":/icons/module.svg"));
    EXPECT_NE(glyph->toolTip(), QStringLiteral("Binary module"));
    EXPECT_TRUE(glyph->toolTip().contains(QStringLiteral("factory")));
}

TEST(UiCanvasScene, TheMarkIsPaintedInTheSchemeTheSceneWasGivenAndNotTheApplicationsInk) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();

    state.doc.add_module("", "src", "a");

    canvas_scene scene(state, callbacks);
    for (const QPalette& scheme : {light_scheme(), dark_scheme()}) {
        const canvas_palette colors = canvas_colors(scheme);
        scene.set_colors(colors);
        scene.rebuild();

        glyph_item* glyph = node_glyph(scene, "a");
        ASSERT_NE(glyph, nullptr);
        EXPECT_EQ(glyph->ink(), colors.node_title);
    }
}

TEST(UiCanvasScene, TheNameStartsAfterTheMarkAndStillStopsAtTheEdgeOfTheNode) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();

    const std::string long_name = "a_module_whose_name_is_far_longer_than_the_node_is_wide";
    state.doc.add_module("", "src", long_name);

    canvas_scene scene(state, callbacks);
    scene.rebuild();

    glyph_item* glyph = node_glyph(scene, long_name);
    QGraphicsSimpleTextItem* title = node_title(scene, long_name);
    ASSERT_NE(glyph, nullptr);
    ASSERT_NE(title, nullptr);

    EXPECT_GE(title->pos().x(), glyph->pos().x() + glyph->boundingRect().width());
    EXPECT_NE(title->text(), QString::fromStdString(long_name));
    EXPECT_LE(title->pos().x() + title->boundingRect().width(), atp::studio::ui::node_width);
    EXPECT_EQ(title->toolTip(), QString::fromStdString(long_name));
}

TEST(UiCanvasScene, TheMarkReachesThePixelsInTheInkItWasGiven) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();

    state.doc.add_module("", "src", "a");

    canvas_scene scene(state, callbacks);
    const canvas_palette colors = canvas_colors(light_scheme());
    scene.set_colors(colors);
    scene.rebuild();

    glyph_item* glyph = node_glyph(scene, "a");
    ASSERT_NE(glyph, nullptr);

    const QRectF where = glyph->sceneBoundingRect();
    QImage shot(64, 64, QImage::Format_ARGB32_Premultiplied);
    shot.fill(Qt::transparent);
    QPainter painter(&shot);
    scene.render(&painter, QRectF(0.0, 0.0, 64.0, 64.0), where);
    painter.end();

    int inked = 0;
    for (int y = 0; y < shot.height(); ++y) {
        for (int x = 0; x < shot.width(); ++x) {
            const QColor pixel = shot.pixelColor(x, y);
            if (pixel.alpha() > 200 && pixel.rgb() == colors.node_title.rgb()) {
                ++inked;
            }
        }
    }
    EXPECT_GT(inked, 40);
}

TEST(UiCanvasScene, TheNodeBeingDraggedComesOutFromUnderTheOthers) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();

    state.doc.add_module("", "src", "a");
    state.doc.add_module("", "mid", "b");
    state.doc.add_module("", "sink", "c");

    canvas_scene scene(state, callbacks);
    scene.rebuild();
    ASSERT_NE(stacking(scene).front(), "a");

    drag_node(scene, node_pos(scene, "a") + QPointF(10.0, 10.0), node_pos(scene, "a") + QPointF(30.0, 10.0));

    EXPECT_EQ(stacking(scene).front(), "a");
}

TEST(UiCanvasScene, ANodeDraggedToTheFrontStaysThereAcrossARebuild) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();

    state.doc.add_module("", "src", "a");
    state.doc.add_module("", "mid", "b");
    state.doc.add_module("", "sink", "c");

    canvas_scene scene(state, callbacks);
    scene.rebuild();
    drag_node(scene, node_pos(scene, "a") + QPointF(10.0, 10.0), node_pos(scene, "a") + QPointF(30.0, 10.0));
    ASSERT_EQ(stacking(scene).front(), "a");

    scene.rebuild();

    EXPECT_EQ(stacking(scene).front(), "a");
}

TEST(UiCanvasScene, EveryNodeOfADraggedSelectionComesUpTogether) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();

    state.doc.add_module("", "src", "a");
    state.doc.add_module("", "mid", "b");
    state.doc.add_module("", "sink", "c");

    canvas_scene scene(state, callbacks);
    scene.rebuild();
    for (QGraphicsItem* item : scene.items()) {
        if (auto* node = qgraphicsitem_cast<node_item*>(item);
            node != nullptr && (node->child_name() == "a" || node->child_name() == "b")) {
            node->setSelected(true);
        }
    }

    drag_node(scene, node_pos(scene, "b") + QPointF(10.0, 10.0), node_pos(scene, "b") + QPointF(30.0, 10.0));

    const std::vector<std::string> order = stacking(scene);
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order.back(), "c");
}

TEST(UiCanvasScene, ARaisedNodeStillPassesUnderTheLinks) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();

    state.doc.add_module("", "src", "a");
    state.doc.add_module("", "sink", "b");
    state.doc.connect("", "a.out", "b.in");

    canvas_scene scene(state, callbacks);
    scene.rebuild();
    drag_node(scene, node_pos(scene, "a") + QPointF(10.0, 10.0), node_pos(scene, "a") + QPointF(30.0, 10.0));

    ASSERT_EQ(scene.links().size(), 1u);
    const double link_z = scene.links().front()->zValue();
    for (QGraphicsItem* item : scene.items()) {
        if (auto* node = qgraphicsitem_cast<node_item*>(item)) {
            EXPECT_LT(node->zValue(), link_z) << node->child_name();
        }
    }
}

TEST(UiCanvasScene, AModuleAddedToTheLevelOnScreenLandsOverTheRaisedOnes) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();

    state.doc.add_module("", "src", "a");
    state.doc.add_module("", "mid", "b");

    canvas_scene scene(state, callbacks);
    scene.rebuild();
    drag_node(scene, node_pos(scene, "a") + QPointF(10.0, 10.0), node_pos(scene, "a") + QPointF(30.0, 10.0));
    ASSERT_EQ(stacking(scene).front(), "a");

    state.doc.add_module("", "sink", "c");
    scene.rebuild();

    EXPECT_EQ(stacking(scene).front(), "c");
}

TEST(UiCanvasScene, APastedNodeLandsOverTheRaisedOnesToo) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();

    state.doc.add_module("", "src", "a");
    state.doc.add_module("", "mid", "b");

    canvas_scene scene(state, callbacks);
    scene.rebuild();
    drag_node(scene, node_pos(scene, "a") + QPointF(10.0, 10.0), node_pos(scene, "a") + QPointF(30.0, 10.0));
    ASSERT_EQ(stacking(scene).front(), "a");

    scene.clearSelection();
    node_by_name(scene, "b")->setSelected(true);
    QKeyEvent copy(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier);
    QApplication::sendEvent(&scene, &copy);
    QKeyEvent paste(QEvent::KeyPress, Qt::Key_V, Qt::ControlModifier);
    QApplication::sendEvent(&scene, &paste);
    scene.rebuild();

    const std::vector<std::string> order = stacking(scene);
    ASSERT_EQ(order.size(), 3u);
    EXPECT_NE(order.front(), "a");
    EXPECT_NE(order.front(), "b");
}

TEST(UiCanvasScene, TheFirstBuildOfALevelRaisesNothing) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();

    state.doc.add_module("", "src", "a");
    state.doc.add_module("", "mid", "b");
    state.doc.add_module("", "sink", "c");

    canvas_scene scene(state, callbacks);
    scene.rebuild();

    EXPECT_EQ(stacking(scene), (std::vector<std::string>{"c", "b", "a"}));
}

TEST(UiCanvasScene, AProjectTheSceneHasNotSeenStartsWithNoPileOfItsOwn) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();

    state.doc.add_module("", "src", "a");
    state.doc.add_module("", "mid", "b");

    canvas_scene scene(state, callbacks);
    scene.rebuild();
    drag_node(scene, node_pos(scene, "a") + QPointF(10.0, 10.0), node_pos(scene, "a") + QPointF(30.0, 10.0));
    ASSERT_EQ(stacking(scene).front(), "a");

    scene.forget_stacking();
    scene.rebuild();

    EXPECT_EQ(stacking(scene).front(), "b");
}

TEST(UiCanvasScene, ACtrlClickThatOnlyChangesTheSelectionLeavesThePileAlone) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();

    state.doc.add_module("", "src", "a");
    state.doc.add_module("", "mid", "b");
    state.doc.add_module("", "sink", "c");

    canvas_scene scene(state, callbacks);
    scene.rebuild();
    const std::vector<std::string> before = stacking(scene);

    ctrl_click(scene, node_pos(scene, "a") + QPointF(10.0, 10.0));

    ASSERT_EQ(selected_names(scene), (std::vector<std::string>{"a"}));
    EXPECT_EQ(stacking(scene), before);
}

}  // namespace
