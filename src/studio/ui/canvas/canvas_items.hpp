// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_UI_CANVAS_ITEMS_HPP
#define ATP_STUDIO_UI_CANVAS_ITEMS_HPP

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <typeindex>

#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QString>

namespace atp::studio::ui {

/// Node geometry, sized for 8 to 10 ports without scrolling.
inline constexpr double node_width = 180.0;
inline constexpr double node_header = 34.0;
inline constexpr double pin_row = 18.0;
inline constexpr double pin_radius = 5.0;

/// Tooltip for a port pin: the port name, then what travels through it. The hollow ring is the mark
/// a universal input can be recognised by at a glance, this is the same fact in words — and the only
/// place the type name is readable at all, so a typed port gets one too.
/// @param port port name as shown on the node, without the child prefix
/// @param type resolved port type, nullopt when the factory is not loaded
/// @param output whether the pin is an output
[[nodiscard]] QString pin_tooltip(const std::string& port, const std::optional<std::type_index>& type, bool output);

/// A port pin on a node.
///
/// The pin is handed the port type rather than a ready colour: how a type looks is one decision and
/// it belongs here, next to the geometry, and a pin that remembers its type can be asked whether a
/// link being dragged may land on it.
class pin_item final : public QGraphicsEllipseItem {
   public:
    enum { Type = UserType + 1 };

    /// A concrete type is a filled circle whose hue comes from the type name; a universal input
    /// (input<std::any>) is a hollow ring — it carries no type colour because it has no type. An
    /// unknown type stays a filled grey circle, so "no type" and "type not known here" do not look
    /// alike. An output of type std::any is not marked: there the type is a restriction rather than
    /// a freedom — it only fits a universal input — so the ring keeps exactly one meaning.
    ///
    /// The missing fill costs nothing in hit testing: QGraphicsEllipseItem::shape() unions the whole
    /// ellipse with the stroke, so a drag started in the middle of the ring still finds the pin.
    pin_item(QGraphicsItem* parent, std::string port_path, bool output, const std::optional<std::type_index>& type);

    [[nodiscard]] int type() const override;
    [[nodiscard]] const std::string& port_path() const;
    [[nodiscard]] bool is_output() const;

    /// Resolved type of the port, nullopt when the factory is not loaded.
    [[nodiscard]] const std::optional<std::type_index>& port_type() const;

    /// Dims the pin while a link is being dragged elsewhere, marking it as a drop the connection
    /// cannot land on. Only the opacity changes — the pen already carries "universal or not".
    /// @param on whether the pin is a legal drop
    void set_eligible(bool on);

    /// Grows the pin while the cursor is over it, answering "am I on it?" before the press. Size is
    /// the one channel still free of meaning — fill carries the type, the pen carries whether the
    /// input is universal, opacity carries whether a dragged link may land here. Scaling happens
    /// around the item's own centre, so scenePos() and the links anchored to it do not move.
    ///
    /// Driven by the hover handlers; public so the effect can be exercised without a live scene.
    /// @param on whether the cursor is over the pin
    void set_hovered(bool on);

   protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;

    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

   private:
    std::string port_path_;
    bool output_;
    std::optional<std::type_index> port_type_;
};

/// A module or subgroup node on the canvas.
class node_item final : public QGraphicsRectItem {
   public:
    enum { Type = UserType + 2 };

    node_item(std::string child_name, bool is_group, double height);

    [[nodiscard]] int type() const override;
    [[nodiscard]] const std::string& child_name() const;
    [[nodiscard]] bool is_group() const;

    /// Frames the node as the group a drop would land in, or takes the frame off again. Drawn in the
    /// palette's highlight colour, the way the project tree frames its drop target: the same
    /// question is being answered in both views, so it gets the same answer.
    /// @param on whether the node is the current drop target
    void set_drop_target(bool on);

    std::function<void(node_item&)> on_moved;

   protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;

   private:
    std::string child_name_;
    bool group_;
};

/// A connection between two pins, carrying an optional monitoring label.
class link_item final : public QGraphicsPathItem {
   public:
    enum { Type = UserType + 3 };

    explicit link_item(std::size_t index);

    [[nodiscard]] int type() const override;

    /// Index of the connection within its group.
    [[nodiscard]] std::size_t index() const;

    void set_endpoints(QPointF from, QPointF to);

    /// Highlights the link when its write generation grew since the last poll.
    void set_hot(bool hot);

    void set_label(const QString& text);

   private:
    std::size_t index_;
    QGraphicsSimpleTextItem* label_ = nullptr;
};

/// Group boundary stub: a short dangling segment from a child pin towards the "edge", showing that
/// the port is exported out of the group. From inside the group this is the only visible mark of a
/// link leading to the parent; the segment has a fixed length, because an infinite scene has no
/// real edge. Selectable but not movable — it only follows its pin.
class stub_item final : public QGraphicsPathItem {
   public:
    enum { Type = UserType + 4 };

    /// Stroked in the colour of the port type, and in the neutral ring colour when the port is a
    /// universal input — the stub is the only mark that port carries once it is exported.
    stub_item(bool is_output, std::string alias, const std::optional<std::type_index>& type);

    [[nodiscard]] int type() const override;
    [[nodiscard]] bool is_output() const;
    [[nodiscard]] const std::string& alias() const;

    /// Anchors the stub to a pin: an exported output leads to the right (an arrow away from the
    /// pin), an input arrives from the left (an arrow into it).
    void set_anchor(QPointF pin_scene_pos);

   private:
    bool is_output_;
    std::string alias_;
    QGraphicsSimpleTextItem* label_ = nullptr;
};

}  // namespace atp::studio::ui

#endif
