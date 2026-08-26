// SPDX-License-Identifier: Apache-2.0
#include "canvas/canvas_items.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <utility>

#include <QBrush>
#include <QColor>
#include <QGraphicsSceneHoverEvent>
#include <QPainterPath>
#include <QPen>
#include <QRectF>

#include <atp/studio/port_types.hpp>

namespace atp::studio::ui {
namespace {

constexpr int drop_frame_width = 2;
constexpr int node_frame_width = 1;
constexpr double typed_pin_pen_width = 1.0;
constexpr double universal_pin_pen_width = 2.0;
constexpr double dimmed_pin_opacity = 0.25;
constexpr double hovered_pin_scale = 1.6;
constexpr double idle_link_width = 1.5;
constexpr double hot_link_width = 3.0;
constexpr double arrow_head = 5.0;
constexpr double label_gap = 4.0;
constexpr double label_rise = 16.0;
constexpr double grab_radius = 8.0;

QColor type_color(const std::optional<std::type_index>& type, const canvas_palette& colors) {
    if (!type) {
        return QColor::fromHsl(0, 0, colors.type_lightness);
    }
    const std::string_view name = type->name();
    std::uint64_t hash = 14695981039346656037ull;
    for (const char c : name) {
        hash = (hash ^ static_cast<unsigned char>(c)) * 1099511628211ull;
    }
    return QColor::fromHsl(static_cast<int>(hash % 360), colors.type_saturation, colors.type_lightness);
}

bool universal_input(const std::optional<std::type_index>& type, bool output) {
    return !output && type && studio::is_universal(*type);
}

QColor ring_ground(QGraphicsItem* parent, const canvas_palette& colors) {
    if (auto* node = qgraphicsitem_cast<node_item*>(parent)) {
        return node->brush().color();
    }
    return colors.pin_outline;
}

}  // namespace

QString pin_tooltip(const std::string& port, const std::optional<std::type_index>& type, bool output) {
    const QString name = QString::fromStdString(port);
    if (!type) {
        return name + QStringLiteral("\nunknown type (factory not loaded)");
    }
    if (studio::is_universal(*type)) {
        return name + (output ? QStringLiteral("\nany (fits a universal input only)")
                              : QStringLiteral("\nany (accepts any type)"));
    }
    return name + QStringLiteral("\n") + QString::fromStdString(type->name());
}

pin_item::pin_item(QGraphicsItem* parent,
                   std::string port_path,
                   bool output,
                   const std::optional<std::type_index>& type,
                   const canvas_palette& colors)
    : QGraphicsEllipseItem(-pin_radius, -pin_radius, 2 * pin_radius, 2 * pin_radius, parent)
    , port_path_(std::move(port_path))
    , output_(output)
    , port_type_(type) {
    const bool universal = universal_input(type, output);
    setBrush(universal ? QBrush(Qt::NoBrush) : QBrush(type_color(type, colors)));
    setPen(QPen(universal ? colors.universal_ink : ring_ground(parent, colors),
                universal ? universal_pin_pen_width : typed_pin_pen_width));
    setAcceptHoverEvents(true);
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

const std::optional<std::type_index>& pin_item::port_type() const {
    return port_type_;
}

void pin_item::set_eligible(bool on) {
    setOpacity(on ? 1.0 : dimmed_pin_opacity);
}

void pin_item::set_hovered(bool on) {
    setScale(on ? hovered_pin_scale : 1.0);
}

void pin_item::hoverEnterEvent(QGraphicsSceneHoverEvent* event) {
    set_hovered(true);
    QGraphicsEllipseItem::hoverEnterEvent(event);
}

void pin_item::hoverLeaveEvent(QGraphicsSceneHoverEvent* event) {
    set_hovered(false);
    QGraphicsEllipseItem::hoverLeaveEvent(event);
}

node_item::node_item(std::string child_name, bool is_group, double height, const canvas_palette& colors)
    : QGraphicsRectItem(0, 0, node_width, height)
    , child_name_(std::move(child_name))
    , group_(is_group)
    , border_(colors.node_border)
    , drop_(colors.drop_target) {
    setFlags(ItemIsSelectable | ItemSendsGeometryChanges);
    setBrush(QBrush(is_group ? colors.group_fill : colors.node_fill));
    setPen(QPen(border_, node_frame_width));
}

void node_item::set_drop_target(bool on) {
    setPen(on ? QPen(drop_, drop_frame_width) : QPen(border_, node_frame_width));
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

link_item::link_item(std::size_t index, const canvas_palette& colors)
    : index_(index), idle_(colors.link), hot_(colors.link_hot) {
    setFlag(ItemIsSelectable);
    set_hot(false);
    label_ = new QGraphicsSimpleTextItem(this);
    label_->setBrush(QBrush(colors.link_label));
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
    setPen(QPen(hot ? hot_ : idle_, hot ? hot_link_width : idle_link_width));
}

void link_item::set_label(const QString& text) {
    label_->setText(text);
}

stub_item::stub_item(bool is_output,
                     std::string alias,
                     const std::optional<std::type_index>& type,
                     const canvas_palette& colors)
    : is_output_(is_output), alias_(std::move(alias)) {
    setFlag(ItemIsSelectable);
    setPen(QPen(universal_input(type, is_output) ? colors.universal_ink : type_color(type, colors), idle_link_width,
                Qt::DashLine));
    label_ = new QGraphicsSimpleTextItem(this);
    label_->setBrush(QBrush(colors.stub_label));
    label_->setText(QString::fromStdString(alias_));
}

int stub_item::type() const {
    return Type;
}

QRectF stub_item::boundingRect() const {
    const double pad = pen().widthF() / 2.0;
    return path().controlPointRect().adjusted(-pad, -pad, pad, pad).united(shape().controlPointRect());
}

QPainterPath stub_item::shape() const {
    QPainterPath hit;
    hit.addRect(QRectF(outer_.x() - grab_radius, outer_.y() - grab_radius, 2 * grab_radius, 2 * grab_radius));
    hit.addRect(label_->boundingRect().translated(label_->pos()));
    return hit;
}

bool stub_item::is_output() const {
    return is_output_;
}

const std::string& stub_item::alias() const {
    return alias_;
}

void stub_item::set_anchor(QPointF pin_scene_pos) {
    prepareGeometryChange();
    const double dir = is_output_ ? 1.0 : -1.0;
    const QPointF outer = pin_scene_pos + QPointF(dir * stub_length, 0.0);
    outer_ = outer;
    QPainterPath path(pin_scene_pos);
    path.lineTo(outer);
    const QPointF tip = is_output_ ? outer : pin_scene_pos;
    path.moveTo(tip);
    path.lineTo(tip + QPointF(-dir * arrow_head, -arrow_head));
    path.moveTo(tip);
    path.lineTo(tip + QPointF(-dir * arrow_head, arrow_head));
    setPath(path);
    label_->setText(QString::fromStdString(alias_));
    const double offset = is_output_ ? label_gap : -label_gap - label_->boundingRect().width();
    label_->setPos(outer + QPointF(offset, -label_rise));
}

}  // namespace atp::studio::ui
