#ifndef ATP_STUDIO_QT_CANVAS_ITEMS_HPP
#define ATP_STUDIO_QT_CANVAS_ITEMS_HPP

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <typeindex>
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

// Цвет порта — детерминированно из имени типа (FNV-1a → оттенок HSV):
// входы и выходы одного типа выглядят одинаково на всех узлах и между
// запусками; направление и так видно по стороне узла. Неизвестный тип
// (фабрика не загружена) — нейтральный серый.
[[nodiscard]] inline QColor type_color(const std::optional<std::type_index>& type) {
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

class pin_item final : public QGraphicsEllipseItem {
   public:
    enum { Type = UserType + 1 };

    pin_item(QGraphicsItem* parent, std::string port_path, bool output, const QColor& color)
        : QGraphicsEllipseItem(-pin_radius, -pin_radius, 2 * pin_radius, 2 * pin_radius, parent)
        , port_path_(std::move(port_path))
        , output_(output) {
        setBrush(QBrush(color));
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

// Стаб границы группы: короткий висящий сегмент от пина ребёнка к «краю»,
// показывающий, что порт экспонирован наружу (expose_input/expose_output).
// Изнутри группы это единственная видимая метка связи, уходящей к родителю;
// сегмент фиксированной длины, потому что у бесконечной сцены нет реального
// края. Выделяемый, но не перемещаемый — двигается только вслед за пином.
class stub_item final : public QGraphicsPathItem {
   public:
    enum { Type = UserType + 4 };

    stub_item(bool is_output, std::string alias, const QColor& color)
        : is_output_(is_output), alias_(std::move(alias)) {
        setFlag(ItemIsSelectable);
        // Пунктир отличает границу от сплошных внутренних связей; цвет типа
        // порта — тот же, что у пина, чтобы тип читался.
        setPen(QPen(color, 1.5, Qt::DashLine));
        label_ = new QGraphicsSimpleTextItem(this);
        label_->setBrush(QBrush(QColor(180, 180, 190)));
        label_->setText(QString::fromStdString(alias_));
    }

    [[nodiscard]] int type() const override {
        return Type;
    }
    [[nodiscard]] bool is_output() const {
        return is_output_;
    }
    [[nodiscard]] const std::string& alias() const {
        return alias_;
    }

    // Экспонированный выход уходит вправо (стрелка от пина), вход приходит
    // слева (стрелка в пин). Длина сегмента фиксированная.
    void set_anchor(QPointF pin_scene_pos) {
        constexpr double length = 40.0;
        const double dir = is_output_ ? 1.0 : -1.0;
        const QPointF outer = pin_scene_pos + QPointF(dir * length, 0.0);
        QPainterPath path(pin_scene_pos);
        path.lineTo(outer);
        // Наконечник стрелки: у выхода — на внешнем конце, у входа — у пина.
        const QPointF tip = is_output_ ? outer : pin_scene_pos;
        const double head = 5.0;
        path.moveTo(tip);
        path.lineTo(tip + QPointF(-dir * head, -head));
        path.moveTo(tip);
        path.lineTo(tip + QPointF(-dir * head, head));
        setPath(path);
        label_->setText(QString::fromStdString(alias_));
        // Подпись у внешнего конца, немного выше линии.
        label_->setPos(outer + QPointF(is_output_ ? 4.0 : -4.0 - label_->boundingRect().width(), -16.0));
    }

   private:
    bool is_output_;
    std::string alias_;
    QGraphicsSimpleTextItem* label_ = nullptr;
};

}  // namespace atp::studio::qt

#endif  // ATP_STUDIO_QT_CANVAS_ITEMS_HPP
