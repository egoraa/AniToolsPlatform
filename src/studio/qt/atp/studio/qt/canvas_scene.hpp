#ifndef ATP_STUDIO_QT_CANVAS_SCENE_HPP
#define ATP_STUDIO_QT_CANVAS_SCENE_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <tuple>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

#include <QGraphicsLineItem>
#include <QGraphicsScene>
#include <QGraphicsSceneDragDropEvent>
#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>
#include <QMimeData>

#include <atp/studio/add_module.hpp>
#include <atp/studio/layout.hpp>
#include <atp/studio/port_types.hpp>
#include <atp/studio/qt/app_state.hpp>
#include <atp/studio/qt/canvas_items.hpp>
#include <atp/studio/qt/module_mime.hpp>
#include <atp/studio/value_format.hpp>

namespace atp::studio::qt {

// Сцена уровня группы. Перестройка целиком на каждое изменение документа:
// модели маленькие, инкрементальность не окупается. Мониторинг трогает
// только подсветку/подписи связей (update_samples — задача 5).
class canvas_scene final : public QGraphicsScene {
   public:
    canvas_scene(app_state& state, ui_callbacks& callbacks, QObject* parent = nullptr)
        : QGraphicsScene(parent), state_(state), callbacks_(callbacks) {
        QObject::connect(this, &QGraphicsScene::selectionChanged, this, [this] {
            if (rebuilding_) {
                return;  // перестройка дёргает выбор — это не действия пользователя
            }
            std::string selected;
            for (QGraphicsItem* item : selectedItems()) {
                if (auto* node = qgraphicsitem_cast<node_item*>(item)) {
                    selected = node->child_name();
                    break;
                }
            }
            if (selected != state_.selected_child) {
                state_.selected_child = selected;
                if (callbacks_.selection_changed) {
                    callbacks_.selection_changed();
                }
            }
        });
    }

    void rebuild() {
        rebuilding_ = true;
        clear();
        links_.clear();
        pins_.clear();
        prev_writes_.clear();
        temp_link_ = nullptr;
        drag_from_ = nullptr;
        const app::group_node* g = state_.doc.group_at(state_.current_group);
        if (g == nullptr) {
            // группу могли удалить/переименовать — канвас откатывается к корню
            state_.current_group.clear();
            g = state_.doc.group_at("");
        }
        const auto fallback = auto_layout(*g);
        for (const app::child_node& c : g->children) {
            build_node(c, fallback);
        }
        rebuild_links(*g);
        rebuilding_ = false;
    }

    // Обновление концов связей при переносе узлов — без перестройки сцены.
    void update_link_paths() {
        const app::group_node* g = state_.doc.group_at(state_.current_group);
        if (g == nullptr) {
            return;
        }
        for (std::size_t i = 0; i < g->connections.size() && i < links_.size(); ++i) {
            auto from = pins_.find(pin_key(g->connections[i].from, true));
            auto to = pins_.find(pin_key(g->connections[i].to, false));
            if (from != pins_.end() && to != pins_.end()) {
                links_[i]->set_endpoints(from->second->scenePos(), to->second->scenePos());
            }
        }
    }

    [[nodiscard]] const std::vector<link_item*>& links() const {
        return links_;
    }

    // Мониторинг: подсветка связей по росту поколения записи и подписи
    // значений. Сцену не перестраивает — только пены и тексты.
    void update_samples() {
        if (!state_.run.running()) {
            for (link_item* link : links_) {
                link->set_hot(false);
                link->set_label({});
            }
            prev_writes_.clear();
            return;
        }
        for (const session::connection_sample& sample : state_.run.sample_connections()) {
            if (sample.group_path != state_.current_group || sample.index >= links_.size()) {
                continue;
            }
            link_item* link = links_[sample.index];
            link->set_hot(sample.writes != prev_writes_[sample.index]);
            prev_writes_[sample.index] = sample.writes;
            if (sample.value) {
                const auto text = format_value(*sample.value);
                link->set_label(text ? QString::fromStdString(*text)
                                     : QString::fromStdString(sample.value->type().name()));
            }
        }
    }

   protected:
    // Перетаскивание из палитры: принимаем только свой MIME-тип и только на
    // стопе — документ на ходу read-only, как и для мыши с Delete.
    void dragEnterEvent(QGraphicsSceneDragDropEvent* event) override {
        accept_module_drag(event);
    }

    void dragMoveEvent(QGraphicsSceneDragDropEvent* event) override {
        accept_module_drag(event);
    }

    void dropEvent(QGraphicsSceneDragDropEvent* event) override {
        module_mime_payload payload;
        if (state_.run.running() || !decode_module_mime(event->mimeData(), payload)) {
            QGraphicsScene::dropEvent(event);
            return;
        }
        studio::add_module_request request;
        request.group_path = state_.current_group;
        request.factory = payload.factory.toStdString();
        request.factory_version = try_parse_version(payload.version.toStdString());
        request.plugin = std::filesystem::path(payload.plugin.toStdWString());
        request.config_dir = state_.config_dir();
        request.position =
            node_position{static_cast<float>(event->scenePos().x()), static_cast<float>(event->scenePos().y())};
        try {
            const studio::add_module_result result = studio::add_module(state_.doc, request);
            if (!result.warning.empty()) {
                callbacks_.error(QString::fromStdString("warning: " + result.warning));
            }
            callbacks_.document_changed();
        } catch (const std::exception& e) {
            callbacks_.error(QString::fromStdString(std::string("add module: ") + e.what()));
        }
        event->acceptProposedAction();
    }

    void mousePressEvent(QGraphicsSceneMouseEvent* event) override {
        if (!state_.run.running()) {
            if (auto* pin = pin_at(event->scenePos())) {
                drag_from_ = pin;
                temp_link_ =
                    addLine(QLineF(pin->scenePos(), event->scenePos()), QPen(QColor(200, 200, 120), 1.5, Qt::DashLine));
                event->accept();
                return;
            }
        }
        QGraphicsScene::mousePressEvent(event);
    }

    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override {
        if (temp_link_ != nullptr) {
            temp_link_->setLine(QLineF(drag_from_->scenePos(), event->scenePos()));
            event->accept();
            return;
        }
        QGraphicsScene::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override {
        if (temp_link_ != nullptr) {
            removeItem(temp_link_);
            delete temp_link_;
            temp_link_ = nullptr;
            pin_item* target = pin_at(event->scenePos());
            if (target != nullptr && target != drag_from_ && target->is_output() != drag_from_->is_output()) {
                const std::string& from = drag_from_->is_output() ? drag_from_->port_path() : target->port_path();
                const std::string& to = drag_from_->is_output() ? target->port_path() : drag_from_->port_path();
                try {
                    connect_ports(state_.doc, state_.current_group, from, to, describer());
                    callbacks_.document_changed();
                } catch (const std::exception& e) {
                    callbacks_.error(QString::fromStdString(std::string("connect: ") + e.what()));
                }
            }
            drag_from_ = nullptr;
            event->accept();
            return;
        }
        QGraphicsScene::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override {
        for (QGraphicsItem* item : items(event->scenePos())) {
            if (auto* node = qgraphicsitem_cast<node_item*>(item)) {
                if (node->is_group()) {
                    state_.current_group = detail::full_path(state_.current_group, node->child_name());
                    state_.selected_child.clear();
                    callbacks_.document_changed();
                    event->accept();
                    return;
                }
            }
        }
        QGraphicsScene::mouseDoubleClickEvent(event);
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Delete && !state_.run.running()) {
            // связи — по убыванию индексов: удаление сдвигает следующие
            std::vector<std::size_t> link_indices;
            std::vector<std::string> nodes;
            for (QGraphicsItem* item : selectedItems()) {
                if (auto* link = qgraphicsitem_cast<link_item*>(item)) {
                    link_indices.push_back(link->index());
                } else if (auto* node = qgraphicsitem_cast<node_item*>(item)) {
                    nodes.push_back(node->child_name());
                }
            }
            std::ranges::sort(link_indices, std::ranges::greater{});
            try {
                for (std::size_t index : link_indices) {
                    state_.doc.disconnect(state_.current_group, index);
                }
                for (const std::string& name : nodes) {
                    state_.doc.remove_child(state_.current_group, name);
                }
                state_.selected_child.clear();
                callbacks_.document_changed();
            } catch (const std::exception& e) {
                callbacks_.error(QString::fromStdString(std::string("delete: ") + e.what()));
            }
            event->accept();
            return;
        }
        QGraphicsScene::keyPressEvent(event);
    }

   private:
    // ignore() вместо accept() — курсор сам покажет запрет сброса.
    void accept_module_drag(QGraphicsSceneDragDropEvent* event) {
        if (!state_.run.running() && event->mimeData() != nullptr && event->mimeData()->hasFormat(module_mime_type)) {
            event->acceptProposedAction();
        } else {
            event->ignore();
        }
    }

    [[nodiscard]] static std::string pin_key(const std::string& port_path, bool output) {
        return (output ? "o:" : "i:") + port_path;
    }

    [[nodiscard]] pin_item* pin_at(const QPointF& pos) {
        for (QGraphicsItem* item : items(pos)) {
            if (auto* pin = qgraphicsitem_cast<pin_item*>(item)) {
                return pin;
            }
        }
        return nullptr;
    }

    // Описатель для не-Qt слоя (цвета пинов, проверка типов при связывании):
    // кэш app_state, завёрнутый в коллбэк.
    [[nodiscard]] describe_fn describer() {
        return [this](const std::string& factory, const std::optional<version>& ver) {
            return state_.describe_cached(factory, ver);
        };
    }

    void build_node(const app::child_node& c, const std::unordered_map<std::string, node_position>& fallback) {
        const std::string& name = c.module ? c.module->name : c.group->name;
        // (порт, is_output, тип) — тип красит пин: один тип — один цвет
        std::vector<std::tuple<std::string, bool, std::optional<std::type_index>>> ports;
        if (c.module) {
            if (const module_info* info = state_.describe_cached(c.module->factory, c.module->factory_version)) {
                for (const port_info& p : info->inputs) {
                    ports.emplace_back(p.name, false, p.type);
                }
                for (const port_info& p : info->outputs) {
                    ports.emplace_back(p.name, true, p.type);
                }
            }
        } else {
            for (const auto& [alias, path] : c.group->expose_inputs) {
                ports.emplace_back(alias, false, resolve_port_type(*c.group, path, false, describer()));
            }
            for (const auto& [alias, path] : c.group->expose_outputs) {
                ports.emplace_back(alias, true, resolve_port_type(*c.group, path, true, describer()));
            }
        }

        const double height = node_header + pin_row * static_cast<double>(ports.size()) + 6.0;
        auto* node = new node_item(name, !c.module, height);
        addItem(node);

        auto* title = new QGraphicsSimpleTextItem(node);
        title->setBrush(QBrush(Qt::white));
        title->setText(QString::fromStdString(c.module ? name : "[" + name + "]"));
        title->setPos(8, 4);
        if (c.module) {
            auto* subtitle = new QGraphicsSimpleTextItem(node);
            subtitle->setBrush(QBrush(QColor(160, 160, 170)));
            subtitle->setText(QString::fromStdString(c.module->factory));
            subtitle->setPos(8, 18);
            if (state_.describe_cached(c.module->factory, c.module->factory_version) == nullptr) {
                subtitle->setText(subtitle->text() + "  (no factory)");
                subtitle->setBrush(QBrush(QColor(230, 120, 120)));
            }
        }

        double y = node_header;
        for (const auto& [port, is_output, port_type] : ports) {
            auto* label = new QGraphicsSimpleTextItem(node);
            label->setBrush(QBrush(QColor(200, 200, 210)));
            label->setText(QString::fromStdString(port));
            label->setPos(is_output ? node_width - 14.0 - label->boundingRect().width() : 14.0, y);
            auto* pin = new pin_item(node, name + "." + port, is_output, type_color(port_type));
            pin->setPos(is_output ? node_width : 0.0, y + 7.0);
            pins_[pin_key(name + "." + port, is_output)] = pin;
            y += pin_row;
        }

        const std::string full = detail::full_path(state_.current_group, name);
        const auto saved = state_.doc.position(full);
        const node_position p = saved ? *saved : fallback.at(name);
        node->setPos(p.x, p.y);
        node->on_moved = [this, full](node_item& moved) {
            state_.doc.set_position(full, {static_cast<float>(moved.pos().x()), static_cast<float>(moved.pos().y())});
            update_link_paths();
        };
    }

    void rebuild_links(const app::group_node& g) {
        for (std::size_t i = 0; i < g.connections.size(); ++i) {
            auto* link = new link_item(i);
            addItem(link);
            links_.push_back(link);
        }
        update_link_paths();
    }

    app_state& state_;
    ui_callbacks& callbacks_;
    std::unordered_map<std::string, pin_item*> pins_;
    std::vector<link_item*> links_;
    std::unordered_map<std::size_t, std::uint64_t> prev_writes_;  // активность связей между опросами
    pin_item* drag_from_ = nullptr;
    QGraphicsLineItem* temp_link_ = nullptr;
    bool rebuilding_ = false;
};

}  // namespace atp::studio::qt

#endif  // ATP_STUDIO_QT_CANVAS_SCENE_HPP
