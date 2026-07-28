#ifndef ATP_STUDIO_UI_CANVAS_ITEMS_HPP
#define ATP_STUDIO_UI_CANVAS_ITEMS_HPP

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <typeindex>

#include <QColor>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>

namespace atp::studio::ui {

/// Node geometry, sized for 8 to 10 ports without scrolling.
inline constexpr double node_width = 180.0;
inline constexpr double node_header = 34.0;
inline constexpr double pin_row = 18.0;
inline constexpr double pin_radius = 5.0;

/// Port colour, derived deterministically from the type name (FNV-1a → an HSV hue), so that inputs
/// and outputs of one type look the same on every node and across runs; the direction is already
/// visible from the side of the node. An unknown type (the factory is not loaded) is neutral grey.
[[nodiscard]] QColor type_color(const std::optional<std::type_index>& type);

/// A port pin on a node.
class pin_item final : public QGraphicsEllipseItem {
   public:
    enum { Type = UserType + 1 };

    pin_item(QGraphicsItem* parent, std::string port_path, bool output, const QColor& color);

    [[nodiscard]] int type() const override;
    [[nodiscard]] const std::string& port_path() const;
    [[nodiscard]] bool is_output() const;

   private:
    std::string port_path_;
    bool output_;
};

/// A module or subgroup node on the canvas.
class node_item final : public QGraphicsRectItem {
   public:
    enum { Type = UserType + 2 };

    node_item(std::string child_name, bool is_group, double height);

    [[nodiscard]] int type() const override;
    [[nodiscard]] const std::string& child_name() const;
    [[nodiscard]] bool is_group() const;

    std::function<void(node_item&)> on_moved;  // reports the new position into the document sidecar

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

    stub_item(bool is_output, std::string alias, const QColor& color);

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

#endif  // ATP_STUDIO_UI_CANVAS_ITEMS_HPP
