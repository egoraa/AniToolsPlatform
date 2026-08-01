#include "canvas/canvas_items.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <utility>

#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QGraphicsSceneHoverEvent>
#include <QPainterPath>
#include <QPalette>
#include <QPen>

#include <atp/studio/port_types.hpp>

namespace atp::studio::ui {
namespace {

constexpr int drop_frame_width = 2;
constexpr double typed_pin_pen_width = 1.0;
constexpr double universal_pin_pen_width = 2.0;
constexpr double dimmed_pin_opacity = 0.25;
constexpr double hovered_pin_scale = 1.6;

QPen idle_node_pen() {
    return QPen(QColor(140, 140, 150), 1);
}

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

QColor universal_color() {
    return {220, 220, 230};
}

bool universal_input(const std::optional<std::type_index>& type, bool output) {
    return !output && type && studio::is_universal(*type);
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
                   const std::optional<std::type_index>& type)
    : QGraphicsEllipseItem(-pin_radius, -pin_radius, 2 * pin_radius, 2 * pin_radius, parent)
    , port_path_(std::move(port_path))
    , output_(output)
    , port_type_(type) {
    const bool universal = universal_input(type, output);
    setBrush(universal ? QBrush(Qt::NoBrush) : QBrush(type_color(type)));
    setPen(QPen(universal ? universal_color() : QColor(Qt::black),
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

node_item::node_item(std::string child_name, bool is_group, double height)
    : QGraphicsRectItem(0, 0, node_width, height), child_name_(std::move(child_name)), group_(is_group) {
    setFlags(ItemIsSelectable | ItemSendsGeometryChanges);
    setBrush(QBrush(is_group ? QColor(58, 52, 70) : QColor(48, 52, 64)));
    setPen(idle_node_pen());
}

void node_item::set_drop_target(bool on) {
    setPen(on ? QPen(QApplication::palette().color(QPalette::Highlight), drop_frame_width) : idle_node_pen());
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

stub_item::stub_item(bool is_output, std::string alias, const std::optional<std::type_index>& type)
    : is_output_(is_output), alias_(std::move(alias)) {
    setFlag(ItemIsSelectable);
    setPen(QPen(universal_input(type, is_output) ? universal_color() : type_color(type), 1.5, Qt::DashLine));
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
    const QPointF tip = is_output_ ? outer : pin_scene_pos;
    const double head = 5.0;
    path.moveTo(tip);
    path.lineTo(tip + QPointF(-dir * head, -head));
    path.moveTo(tip);
    path.lineTo(tip + QPointF(-dir * head, head));
    setPath(path);
    label_->setText(QString::fromStdString(alias_));
    label_->setPos(outer + QPointF(is_output_ ? 4.0 : -4.0 - label_->boundingRect().width(), -16.0));
}

}  // namespace atp::studio::ui
