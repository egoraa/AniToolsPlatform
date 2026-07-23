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

// Геометрия узла: подобрана под 8-10 портов без прокрутки.
inline constexpr double node_width = 180.0;
inline constexpr double node_header = 34.0;
inline constexpr double pin_row = 18.0;
inline constexpr double pin_radius = 5.0;

// Цвет порта — детерминированно из имени типа (FNV-1a → оттенок HSV):
// входы и выходы одного типа выглядят одинаково на всех узлах и между
// запусками; направление и так видно по стороне узла. Неизвестный тип
// (фабрика не загружена) — нейтральный серый.
[[nodiscard]] QColor type_color(const std::optional<std::type_index>& type);

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

class node_item final : public QGraphicsRectItem {
   public:
    enum { Type = UserType + 2 };

    node_item(std::string child_name, bool is_group, double height);

    [[nodiscard]] int type() const override;
    [[nodiscard]] const std::string& child_name() const;
    [[nodiscard]] bool is_group() const;

    std::function<void(node_item&)> on_moved;  // позиция → документ (sidecar)

   protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;

   private:
    std::string child_name_;
    bool group_;
};

class link_item final : public QGraphicsPathItem {
   public:
    enum { Type = UserType + 3 };

    explicit link_item(std::size_t index);

    [[nodiscard]] int type() const override;
    [[nodiscard]] std::size_t index() const;

    void set_endpoints(QPointF from, QPointF to);

    // Подсветка активности: поколение записи выросло с прошлого опроса.
    void set_hot(bool hot);

    void set_label(const QString& text);

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

    stub_item(bool is_output, std::string alias, const QColor& color);

    [[nodiscard]] int type() const override;
    [[nodiscard]] bool is_output() const;
    [[nodiscard]] const std::string& alias() const;

    // Экспонированный выход уходит вправо (стрелка от пина), вход приходит
    // слева (стрелка в пин). Длина сегмента фиксированная.
    void set_anchor(QPointF pin_scene_pos);

   private:
    bool is_output_;
    std::string alias_;
    QGraphicsSimpleTextItem* label_ = nullptr;
};

}  // namespace atp::studio::ui

#endif  // ATP_STUDIO_UI_CANVAS_ITEMS_HPP
