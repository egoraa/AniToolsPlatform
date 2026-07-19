#ifndef ATP_STUDIO_QT_CANVAS_SCENE_HPP
#define ATP_STUDIO_QT_CANVAS_SCENE_HPP

#include <algorithm>
#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <QGraphicsLineItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>

#include <atp/studio/layout.hpp>
#include <atp/studio/qt/app_state.hpp>
#include <atp/studio/qt/canvas_items.hpp>

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

   protected:
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
                    state_.doc.connect(state_.current_group, from, to);
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

    void build_node(const app::child_node& c, const std::unordered_map<std::string, node_position>& fallback) {
        const std::string& name = c.module ? c.module->name : c.group->name;
        std::vector<std::pair<std::string, bool>> ports;  // (порт, is_output)
        if (c.module) {
            if (const module_info* info = state_.describe_cached(c.module->factory, c.module->factory_version)) {
                for (const port_info& p : info->inputs) {
                    ports.emplace_back(p.name, false);
                }
                for (const port_info& p : info->outputs) {
                    ports.emplace_back(p.name, true);
                }
            }
        } else {
            for (const auto& [alias, path] : c.group->expose_inputs) {
                ports.emplace_back(alias, false);
            }
            for (const auto& [alias, path] : c.group->expose_outputs) {
                ports.emplace_back(alias, true);
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
        for (const auto& [port, is_output] : ports) {
            auto* label = new QGraphicsSimpleTextItem(node);
            label->setBrush(QBrush(QColor(200, 200, 210)));
            label->setText(QString::fromStdString(port));
            label->setPos(is_output ? node_width - 14.0 - label->boundingRect().width() : 14.0, y);
            auto* pin = new pin_item(node, name + "." + port, is_output);
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
    pin_item* drag_from_ = nullptr;
    QGraphicsLineItem* temp_link_ = nullptr;
    bool rebuilding_ = false;
};

}  // namespace atp::studio::qt

#endif  // ATP_STUDIO_QT_CANVAS_SCENE_HPP
