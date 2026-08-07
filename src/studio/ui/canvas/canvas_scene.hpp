// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_UI_CANVAS_SCENE_HPP
#define ATP_STUDIO_UI_CANVAS_SCENE_HPP

#include "canvas/canvas_items.hpp"
#include "model/app_state.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <QGraphicsLineItem>
#include <QGraphicsScene>
#include <QPoint>

#include <atp/studio/layout.hpp>
#include <atp/studio/port_types.hpp>

namespace atp::studio::ui {

/// Scene of one group level. It is rebuilt whole on every project change: the models are small and
/// incremental updates would not pay off. Monitoring touches only the link highlighting and labels.
class canvas_scene final : public QGraphicsScene {
   public:
    canvas_scene(app_state& state, ui_callbacks& callbacks, QObject* parent = nullptr);

    /// Stops reporting selection changes before the base destructor drops the nodes. Destroying an
    /// item is not a user selecting something else, and by this point the window holding the
    /// callbacks is already gone: Qt deletes its widgets from ~QWidget, after its own members.
    ~canvas_scene() override;

    /// Rebuilds the scene from the current group of the project.
    void rebuild();

    /// Updates the link endpoints after nodes have been dragged, without rebuilding the scene.
    void update_link_paths();

    /// Link items of the current scene, in the group's connection order.
    [[nodiscard]] const std::vector<link_item*>& links() const;

    /// Refreshes the monitoring overlay: links whose write generation grew since the last poll are
    /// highlighted and their value labels updated. Only pens and texts change.
    void update_samples();

    /// Pastes the clipboard inside a subgroup of the group on screen, the way dropping a node on it
    /// puts the node inside. The nodes keep the positions they were copied from — a scene coordinate
    /// of this level means nothing one level down — and nothing is selected, since what arrived is
    /// not on screen.
    ///
    /// Driven by the node menu; public so the choice of target can be exercised without a live menu.
    /// @param child_name subgroup within the group on screen
    void paste_inside(const std::string& child_name);

   protected:
    void dragEnterEvent(QGraphicsSceneDragDropEvent* event) override;

    void dragMoveEvent(QGraphicsSceneDragDropEvent* event) override;

    void dropEvent(QGraphicsSceneDragDropEvent* event) override;

    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;

    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;

    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;

    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;

    void keyPressEvent(QKeyEvent* event) override;

   private:
    void accept_palette_drag(QGraphicsSceneDragDropEvent* event);

    [[nodiscard]] static std::string pin_key(const std::string& port_path, bool output);

    [[nodiscard]] pin_item* pin_at(const QPointF& pos);

    /// Dims every pin a link dragged from `from` cannot land on, leaving the legal drops at full
    /// brightness. A universal input stays bright whatever the output is — that is the promise of the
    /// hollow ring, shown while the link is in the air instead of as an error after the drop.
    void mark_eligible_pins(const pin_item& from);

    /// Brings every pin back to full brightness once the drag is over.
    void clear_pin_eligibility();

    [[nodiscard]] node_item* node_at(const QPointF& pos) const;

    [[nodiscard]] node_item* group_node_at(const QPointF& pos) const;

    void set_drop_target(node_item* node);

    void move_into_group(const std::string& name, const std::string& into);

    /// Pastes the clipboard into the group on screen and selects what arrived.
    /// @param at scene position for the top-left of the block; nullopt offsets the block from where
    ///        it was copied, which is what Ctrl+V should do
    void paste_at(std::optional<node_position> at);

    /// Top-left of the clipboard block, so a keyboard paste can offset it without the model knowing
    /// anything about the canvas; nullopt when nothing in the clipboard has a position.
    [[nodiscard]] std::optional<node_position> clipboard_origin() const;

    /// The node menu: the standard clipboard set plus Delete, acting on the current selection, and
    /// the property gestures, which act on the one node clicked. The caller has already made sure
    /// that node is part of the selection.
    /// @param screen_pos where to pop the menu up
    /// @param where scene position of the click, which is where a paste of this level lands
    /// @param name child the click landed on
    /// @param is_group whether that child is a subgroup, which a paste goes inside instead and which
    ///        has no properties of its own
    void show_node_menu(const QPoint& screen_pos, node_position where, const std::string& name, bool is_group);

    /// Deletes the whole selection — links, exported ports and nodes — as one undo step.
    void delete_selection();

    [[nodiscard]] std::vector<std::string> selected_node_names() const;

    void notify_changed();

    void begin_copy_drag();

    void cancel_drag();

    void end_drag();

    [[nodiscard]] node_item* node_by_name(const std::string& name) const;

    [[nodiscard]] describe_fn describer();

    void build_node(const runtime::child_node& c, const std::unordered_map<std::string, node_position>& fallback);

    void rebuild_links(const runtime::group_node& g);

    void build_stubs(const runtime::group_node& g);

    void add_stub(const runtime::group_node& g, const std::string& alias, const std::string& path, bool is_output);

    void reposition_stubs();

    app_state& state_;
    ui_callbacks& callbacks_;
    std::unordered_map<std::string, pin_item*> pins_;
    std::vector<link_item*> links_;
    std::vector<stub_item*> stubs_;
    std::unordered_map<std::size_t, std::uint64_t> prev_writes_;
    pin_item* drag_from_ = nullptr;
    QGraphicsLineItem* temp_link_ = nullptr;
    struct dragged {
        node_item* item;
        QPointF start;
    };

    node_item* moving_node_ = nullptr;
    std::vector<dragged> moving_;
    QPointF press_pos_;
    std::vector<std::string> drag_copies_;
    bool copy_pending_ = false;
    node_item* pressed_node_ = nullptr;
    node_item* drop_target_ = nullptr;
    std::vector<std::string> select_after_rebuild_;
    bool rebuilding_ = false;
};

}  // namespace atp::studio::ui

#endif
