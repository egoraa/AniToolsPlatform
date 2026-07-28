#ifndef ATP_STUDIO_UI_CANVAS_SCENE_HPP
#define ATP_STUDIO_UI_CANVAS_SCENE_HPP

#include "app_state.hpp"
#include "canvas_items.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <QGraphicsLineItem>
#include <QGraphicsScene>

#include <atp/studio/layout.hpp>
#include <atp/studio/port_types.hpp>

namespace atp::studio::ui {

/// Scene of one group level. It is rebuilt whole on every document change: the models are small and
/// incremental updates would not pay off. Monitoring touches only the link highlighting and labels.
class canvas_scene final : public QGraphicsScene {
   public:
    canvas_scene(app_state& state, ui_callbacks& callbacks, QObject* parent = nullptr);

    /// Rebuilds the scene from the current group of the document.
    void rebuild();

    /// Updates the link endpoints after nodes have been dragged, without rebuilding the scene.
    void update_link_paths();

    /// Link items of the current scene, in the group's connection order.
    [[nodiscard]] const std::vector<link_item*>& links() const;

    /// Refreshes the monitoring overlay: links whose write generation grew since the last poll are
    /// highlighted and their value labels updated. Only pens and texts change.
    void update_samples();

   protected:
    // Dragging from the palette: only our own MIME type is accepted, and only while stopped — the
    // document is read-only while running, as it is for the mouse and Delete.
    void dragEnterEvent(QGraphicsSceneDragDropEvent* event) override;

    void dragMoveEvent(QGraphicsSceneDragDropEvent* event) override;

    void dropEvent(QGraphicsSceneDragDropEvent* event) override;

    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;

    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;

    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;

    // A right click on a child pin exports the port out of the group. The menu is driven by
    // exec() plus a comparison of the result: no signals, hence no Q_OBJECT and no moc.
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;

    void keyPressEvent(QKeyEvent* event) override;

   private:
    // ignore() rather than accept() — the cursor then shows the drop is not allowed. Both palette
    // formats are accepted: a module and a group.
    void accept_palette_drag(QGraphicsSceneDragDropEvent* event);

    [[nodiscard]] static std::string pin_key(const std::string& port_path, bool output);

    [[nodiscard]] pin_item* pin_at(const QPointF& pos);

    // Describer for the non-Qt layer (pin colours, type checks while connecting): the app_state
    // cache wrapped into a callback.
    [[nodiscard]] describe_fn describer();

    void build_node(const runtime::child_node& c, const std::unordered_map<std::string, node_position>& fallback);

    void rebuild_links(const runtime::group_node& g);

    // Boundary stubs: one per exported port of the current group. An input is exported through a
    // child's input pin (left side), an output through its output pin (right side). A missing pin
    // (the factory is not loaded) skips the stub, just as links with missing ends are skipped.
    void build_stubs(const runtime::group_node& g);

    void add_stub(const runtime::group_node& g, const std::string& alias, const std::string& path, bool is_output);

    // Stub ends are anchored to child pins, so dragging a node moves the stub too. The pin is
    // looked up again by alias through the group's exports — no separate pin vector is kept, the
    // models being small.
    void reposition_stubs();

    app_state& state_;
    ui_callbacks& callbacks_;
    std::unordered_map<std::string, pin_item*> pins_;
    std::vector<link_item*> links_;
    std::vector<stub_item*> stubs_;                               // exported ports of the current group
    std::unordered_map<std::size_t, std::uint64_t> prev_writes_;  // link activity between polls
    pin_item* drag_from_ = nullptr;
    QGraphicsLineItem* temp_link_ = nullptr;
    bool rebuilding_ = false;
};

}  // namespace atp::studio::ui

#endif  // ATP_STUDIO_UI_CANVAS_SCENE_HPP
