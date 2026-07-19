#ifndef ATP_STUDIO_QT_CANVAS_ITEMS_HPP
#define ATP_STUDIO_QT_CANVAS_ITEMS_HPP

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <string>
#include <utility>

#include <QBrush>
#include <QColor>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QPainterPath>
#include <QPen>

namespace atp::studio::qt {

// Геометрия узла: подобрана под 8-10 портов без прокрутки.
inline constexpr double node_width = 180.0;
inline constexpr double node_header = 34.0;
inline constexpr double pin_row = 18.0;
inline constexpr double pin_radius = 5.0;

class pin_item final : public QGraphicsEllipseItem {
   public:
    enum { Type = UserType + 1 };

    pin_item(QGraphicsItem* parent, std::string port_path, bool output)
        : QGraphicsEllipseItem(-pin_radius, -pin_radius, 2 * pin_radius, 2 * pin_radius, parent)
        , port_path_(std::move(port_path))
        , output_(output) {
        setBrush(QBrush(output ? QColor(120, 200, 120) : QColor(120, 160, 220)));
        setPen(QPen(Qt::black, 1));
    }

    [[nodiscard]] int type() const override {
        return Type;
    }
    [[nodiscard]] const std::string& port_path() const {
        return port_path_;
    }
    [[nodiscard]] bool is_output() const {
        return output_;
    }

   private:
    std::string port_path_;
    bool output_;
};

class node_item final : public QGraphicsRectItem {
   public:
    enum { Type = UserType + 2 };

    node_item(std::string child_name, bool is_group, double height)
        : QGraphicsRectItem(0, 0, node_width, height), child_name_(std::move(child_name)), group_(is_group) {
        setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
        setBrush(QBrush(is_group ? QColor(58, 52, 70) : QColor(48, 52, 64)));
        setPen(QPen(QColor(140, 140, 150), 1));
    }

    [[nodiscard]] int type() const override {
        return Type;
    }
    [[nodiscard]] const std::string& child_name() const {
        return child_name_;
    }
    [[nodiscard]] bool is_group() const {
        return group_;
    }

    std::function<void(node_item&)> on_moved;  // позиция → документ (sidecar)

   protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override {
        if (change == ItemPositionHasChanged && on_moved) {
            on_moved(*this);
        }
        return QGraphicsRectItem::itemChange(change, value);
    }

   private:
    std::string child_name_;
    bool group_;
};

class link_item final : public QGraphicsPathItem {
   public:
    enum { Type = UserType + 3 };

    explicit link_item(std::size_t index) : index_(index) {
        setFlag(ItemIsSelectable);
        set_hot(false);
        label_ = new QGraphicsSimpleTextItem(this);
        label_->setBrush(QBrush(QColor(190, 240, 190)));
    }

    [[nodiscard]] int type() const override {
        return Type;
    }
    [[nodiscard]] std::size_t index() const {
        return index_;
    }

    void set_endpoints(QPointF from, QPointF to) {
        QPainterPath path(from);
        const double dx = std::max(40.0, std::abs(to.x() - from.x()) * 0.5);
        path.cubicTo(from + QPointF(dx, 0), to - QPointF(dx, 0), to);
        setPath(path);
        label_->setPos((from + to) / 2 + QPointF(6, -14));
    }

    // Подсветка активности: поколение записи выросло с прошлого опроса.
    void set_hot(bool hot) {
        setPen(QPen(hot ? QColor(110, 230, 110) : QColor(150, 150, 160), hot ? 3.0 : 1.5));
    }

    void set_label(const QString& text) {
        label_->setText(text);
    }

   private:
    std::size_t index_;
    QGraphicsSimpleTextItem* label_ = nullptr;
};

}  // namespace atp::studio::qt

#endif  // ATP_STUDIO_QT_CANVAS_ITEMS_HPP
