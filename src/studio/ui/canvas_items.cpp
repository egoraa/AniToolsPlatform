#include "canvas_items.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <utility>

#include <QBrush>
#include <QPainterPath>
#include <QPen>

namespace atp::studio::ui {

QColor type_color(const std::optional<std::type_index>& type) {
    if (!type) {
        return {150, 150, 150};
    }
    const std::string_view name = type->name();
    std::uint64_t hash = 14695981039346656037ull;
    for (const char c : name) {
        hash = (hash ^ static_cast<unsigned char>(c)) * 1099511628211ull;
    }
    return QColor::fromHsv(static_cast<int>(hash % 360), 170, 220);
}

pin_item::pin_item(QGraphicsItem* parent, std::string port_path, bool output, const QColor& color)
    : QGraphicsEllipseItem(-pin_radius, -pin_radius, 2 * pin_radius, 2 * pin_radius, parent)
    , port_path_(std::move(port_path))
    , output_(output) {
    setBrush(QBrush(color));
    setPen(QPen(Qt::black, 1));
}

int pin_item::type() const {
    return Type;
}

const std::string& pin_item::port_path() const {
    return port_path_;
}

bool pin_item::is_output() const {
    return output_;
}

node_item::node_item(std::string child_name, bool is_group, double height)
    : QGraphicsRectItem(0, 0, node_width, height), child_name_(std::move(child_name)), group_(is_group) {
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setBrush(QBrush(is_group ? QColor(58, 52, 70) : QColor(48, 52, 64)));
    setPen(QPen(QColor(140, 140, 150), 1));
}

int node_item::type() const {
    return Type;
}

const std::string& node_item::child_name() const {
    return child_name_;
}

bool node_item::is_group() const {
    return group_;
}

QVariant node_item::itemChange(GraphicsItemChange change, const QVariant& value) {
    if (change == ItemPositionHasChanged && on_moved) {
        on_moved(*this);
    }
    return QGraphicsRectItem::itemChange(change, value);
}

link_item::link_item(std::size_t index) : index_(index) {
    setFlag(ItemIsSelectable);
    set_hot(false);
    label_ = new QGraphicsSimpleTextItem(this);
    label_->setBrush(QBrush(QColor(190, 240, 190)));
}

int link_item::type() const {
    return Type;
}

std::size_t link_item::index() const {
    return index_;
}

void link_item::set_endpoints(QPointF from, QPointF to) {
    QPainterPath path(from);
    const double dx = std::max(40.0, std::abs(to.x() - from.x()) * 0.5);
    path.cubicTo(from + QPointF(dx, 0), to - QPointF(dx, 0), to);
    setPath(path);
    label_->setPos((from + to) / 2 + QPointF(6, -14));
}

void link_item::set_hot(bool hot) {
    setPen(QPen(hot ? QColor(110, 230, 110) : QColor(150, 150, 160), hot ? 3.0 : 1.5));
}

void link_item::set_label(const QString& text) {
    label_->setText(text);
}

stub_item::stub_item(bool is_output, std::string alias, const QColor& color)
    : is_output_(is_output), alias_(std::move(alias)) {
    setFlag(ItemIsSelectable);
    // The dashes tell a boundary from the solid internal links, while the colour repeats the pin's
    // so the type stays readable.
    setPen(QPen(color, 1.5, Qt::DashLine));
    label_ = new QGraphicsSimpleTextItem(this);
    label_->setBrush(QBrush(QColor(180, 180, 190)));
    label_->setText(QString::fromStdString(alias_));
}

int stub_item::type() const {
    return Type;
}

bool stub_item::is_output() const {
    return is_output_;
}

const std::string& stub_item::alias() const {
    return alias_;
}

void stub_item::set_anchor(QPointF pin_scene_pos) {
    constexpr double length = 40.0;
    const double dir = is_output_ ? 1.0 : -1.0;
    const QPointF outer = pin_scene_pos + QPointF(dir * length, 0.0);
    QPainterPath path(pin_scene_pos);
    path.lineTo(outer);
    // The arrow head sits at the outer end for an output and at the pin for an input.
    const QPointF tip = is_output_ ? outer : pin_scene_pos;
    const double head = 5.0;
    path.moveTo(tip);
    path.lineTo(tip + QPointF(-dir * head, -head));
    path.moveTo(tip);
    path.lineTo(tip + QPointF(-dir * head, head));
    setPath(path);
    label_->setText(QString::fromStdString(alias_));
    // The label goes at the outer end, slightly above the line.
    label_->setPos(outer + QPointF(is_output_ ? 4.0 : -4.0 - label_->boundingRect().width(), -16.0));
}

}  // namespace atp::studio::ui
