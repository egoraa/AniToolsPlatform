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

// Сцена уровня группы. Перестройка целиком на каждое изменение документа:
// модели маленькие, инкрементальность не окупается. Мониторинг трогает
// только подсветку/подписи связей (update_samples — задача 5).
class canvas_scene final : public QGraphicsScene {
   public:
    canvas_scene(app_state& state, ui_callbacks& callbacks, QObject* parent = nullptr);

    void rebuild();

    // Обновление концов связей при переносе узлов — без перестройки сцены.
    void update_link_paths();

    [[nodiscard]] const std::vector<link_item*>& links() const;

    // Мониторинг: подсветка связей по росту поколения записи и подписи
    // значений. Сцену не перестраивает — только пены и тексты.
    void update_samples();

   protected:
    // Перетаскивание из палитры: принимаем только свой MIME-тип и только на
    // стопе — документ на ходу read-only, как и для мыши с Delete.
    void dragEnterEvent(QGraphicsSceneDragDropEvent* event) override;

    void dragMoveEvent(QGraphicsSceneDragDropEvent* event) override;

    void dropEvent(QGraphicsSceneDragDropEvent* event) override;

    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;

    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;

    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;

    // Правый клик по пину ребёнка — экспонировать порт наружу группы. Меню
    // через exec()+сравнение возврата: без сигналов, значит без Q_OBJECT/moc.
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;

    void keyPressEvent(QKeyEvent* event) override;

   private:
    // ignore() вместо accept() — курсор сам покажет запрет сброса.
    void accept_module_drag(QGraphicsSceneDragDropEvent* event);

    [[nodiscard]] static std::string pin_key(const std::string& port_path, bool output);

    [[nodiscard]] pin_item* pin_at(const QPointF& pos);

    // Описатель для не-Qt слоя (цвета пинов, проверка типов при связывании):
    // кэш app_state, завёрнутый в коллбэк.
    [[nodiscard]] describe_fn describer();

    void build_node(const runtime::child_node& c, const std::unordered_map<std::string, node_position>& fallback);

    void rebuild_links(const runtime::group_node& g);

    // Стабы границы: по одному на каждый exposed-порт текущей группы. Вход
    // экспонируется через входной пин ребёнка (левая сторона), выход — через
    // выходной (правая). Пин отсутствует (фабрика не загружена) — стаб
    // пропускаем, как связи с недостающими концами.
    void build_stubs(const runtime::group_node& g);

    void add_stub(const runtime::group_node& g, const std::string& alias, const std::string& path, bool is_output);

    // Концы стабов привязаны к пинам детей: при переносе узла пин двигается —
    // двигаем и стаб. Пин ищем заново по алиасу через expose_* текущей группы
    // (отдельный вектор пинов не держим — моделей мало).
    void reposition_stubs();

    app_state& state_;
    ui_callbacks& callbacks_;
    std::unordered_map<std::string, pin_item*> pins_;
    std::vector<link_item*> links_;
    std::vector<stub_item*> stubs_;  // визуализация exposed-портов текущей группы
    std::unordered_map<std::size_t, std::uint64_t> prev_writes_;  // активность связей между опросами
    pin_item* drag_from_ = nullptr;
    QGraphicsLineItem* temp_link_ = nullptr;
    bool rebuilding_ = false;
};

}  // namespace atp::studio::ui

#endif  // ATP_STUDIO_UI_CANVAS_SCENE_HPP
