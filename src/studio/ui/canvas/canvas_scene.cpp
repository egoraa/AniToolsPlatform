// SPDX-License-Identifier: Apache-2.0
#include "canvas/canvas_scene.hpp"

#include "model/clipboard_actions.hpp"
#include "model/create_group.hpp"
#include "model/drag_payloads.hpp"
#include "model/property_actions.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <ranges>
#include <set>
#include <tuple>
#include <typeindex>

#include <QAction>
#include <QBrush>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneDragDropEvent>
#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMenu>
#include <QMimeData>
#include <QPen>
#include <QStringList>
#include <QTimer>

#include <atp/studio/add_module.hpp>
#include <atp/studio/node_ref.hpp>

namespace atp::studio::ui {
namespace {

constexpr float paste_offset = 30.0f;

bool drop_allowed(const pin_item& from, const pin_item& to) {
    const pin_item& out = from.is_output() ? from : to;
    const pin_item& in = from.is_output() ? to : from;
    if (!out.port_type() || !in.port_type()) {
        return true;
    }
    return types_compatible(*out.port_type(), *in.port_type());
}

}  // namespace

canvas_scene::canvas_scene(app_state& state, ui_callbacks& callbacks, QObject* parent)
    : QGraphicsScene(parent), state_(state), callbacks_(callbacks) {
    QObject::connect(this, &QGraphicsScene::selectionChanged, this, [this] {
        if (rebuilding_) {
            return;
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

canvas_scene::~canvas_scene() {
    disconnect(this, &QGraphicsScene::selectionChanged, this, nullptr);
}

void canvas_scene::rebuild() {
    rebuilding_ = true;
    clear();
    links_.clear();
    stubs_.clear();
    pins_.clear();
    prev_writes_.clear();
    temp_link_ = nullptr;
    drag_from_ = nullptr;
    moving_.clear();
    moving_node_ = nullptr;
    pressed_node_ = nullptr;
    copy_pending_ = false;
    drop_target_ = nullptr;
    const runtime::group_node* g = state_.doc.group_at(state_.current_group);
    if (g == nullptr) {
        state_.current_group.clear();
        g = state_.doc.group_at("");
    }
    const auto fallback = auto_layout(*g);
    for (const runtime::child_node& c : g->modules) {
        build_node(c, fallback);
    }
    rebuild_links(*g);
    build_stubs(*g);
    rebuilding_ = false;

    for (const std::string& name : select_after_rebuild_) {
        if (node_item* item = node_by_name(name)) {
            item->setSelected(true);
        }
    }
    select_after_rebuild_.clear();
}

void canvas_scene::update_link_paths() {
    const runtime::group_node* g = state_.doc.group_at(state_.current_group);
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
    reposition_stubs();
}

const std::vector<link_item*>& canvas_scene::links() const {
    return links_;
}

void canvas_scene::update_samples() {
    if (!state_.view->running()) {
        for (link_item* link : links_) {
            link->set_hot(false);
            link->set_label({});
        }
        prev_writes_.clear();
        return;
    }
    for (const session::connection_sample& sample : state_.view->sample_connections()) {
        if (sample.group_path != state_.current_group || sample.index >= links_.size()) {
            continue;
        }
        link_item* link = links_[sample.index];
        link->set_hot(sample.writes != prev_writes_[sample.index]);
        prev_writes_[sample.index] = sample.writes;
    }
}

void canvas_scene::dragEnterEvent(QGraphicsSceneDragDropEvent* event) {
    accept_palette_drag(event);
}

void canvas_scene::dragMoveEvent(QGraphicsSceneDragDropEvent* event) {
    accept_palette_drag(event);
}

void canvas_scene::dropEvent(QGraphicsSceneDragDropEvent* event) {
    if (state_.view->running()) {
        QGraphicsScene::dropEvent(event);
        return;
    }
    const node_position where{static_cast<float>(event->scenePos().x()), static_cast<float>(event->scenePos().y())};
    if (is_group_mime(event->mimeData())) {
        create_group(state_, callbacks_, where);
        event->acceptProposedAction();
        return;
    }
    module_mime_payload payload;
    if (!decode_module_mime(event->mimeData(), payload)) {
        QGraphicsScene::dropEvent(event);
        return;
    }
    studio::add_module_request request;
    request.group_path = state_.current_group;
    request.factory = payload.factory.toStdString();
    request.factory_version = try_parse_version(payload.version.toStdString());
    request.plugin = std::filesystem::path(payload.plugin.toStdWString());
    request.config_dir = state_.config_dir();
    request.position = where;
    try {
        const studio::add_module_result result = studio::add_module(state_.doc, request);
        if (!result.warning.empty()) {
            callbacks_.error(QString::fromStdString("warning: " + result.warning));
        }
        callbacks_.project_changed();
    } catch (const std::exception& e) {
        callbacks_.error(QString::fromStdString(std::string("add module: ") + e.what()));
    }
    event->acceptProposedAction();
}

void canvas_scene::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() == Qt::RightButton && node_at(event->scenePos()) != nullptr) {
        event->accept();
        return;
    }
    if (!state_.view->running() && event->button() == Qt::LeftButton) {
        if (auto* pin = pin_at(event->scenePos())) {
            drag_from_ = pin;
            temp_link_ =
                addLine(QLineF(pin->scenePos(), event->scenePos()), QPen(QColor(200, 200, 120), 1.5, Qt::DashLine));
            mark_eligible_pins(*pin);
            event->accept();
            return;
        }
        press_pos_ = event->scenePos();
        node_item* pressed = node_at(event->scenePos());
        if (pressed != nullptr && (event->modifiers() & Qt::ControlModifier) != 0) {
            std::vector<std::string> names = selected_node_names();
            if (std::ranges::find(names, pressed->child_name()) == names.end()) {
                names = {pressed->child_name()};
            }
            for (const std::string& name : names) {
                if (node_item* item = node_by_name(name)) {
                    moving_.push_back({item, item->pos()});
                }
            }
            moving_node_ = pressed;
            pressed_node_ = pressed;
            copy_pending_ = true;
            event->accept();
            return;
        }
        moving_node_ = pressed;
    }
    QGraphicsScene::mousePressEvent(event);
    if (moving_node_ != nullptr) {
        for (QGraphicsItem* item : selectedItems()) {
            if (auto* node = qgraphicsitem_cast<node_item*>(item)) {
                moving_.push_back({node, node->pos()});
            }
        }
    }
}

void canvas_scene::begin_copy_drag() {
    copy_pending_ = false;
    pressed_node_ = nullptr;

    std::vector<std::string> names;
    names.reserve(moving_.size());
    for (const dragged& d : moving_) {
        names.push_back(d.item->child_name());
    }
    std::vector<std::string> copies;
    for (const std::string& name : names) {
        try {
            const std::string new_name = state_.doc.copy_child(state_.current_group, name, state_.current_group);
            if (const auto at = state_.doc.position(node_ref{state_.current_group, name}.full())) {
                state_.doc.set_position(node_ref{state_.current_group, new_name}.full(), *at);
            }
            copies.push_back(new_name);
        } catch (const std::exception& e) {
            callbacks_.error(QString::fromStdString(std::string("copy: ") + e.what()));
        }
    }
    moving_.clear();
    moving_node_ = nullptr;
    if (copies.empty()) {
        return;
    }

    select_after_rebuild_ = copies;
    callbacks_.project_changed();

    for (const std::string& name : copies) {
        if (node_item* item = node_by_name(name)) {
            moving_.push_back({item, item->pos()});
        }
    }
    moving_node_ = moving_.empty() ? nullptr : moving_.front().item;
    drag_copies_ = std::move(copies);
}

void canvas_scene::cancel_drag() {
    for (const dragged& d : moving_) {
        d.item->setPos(d.start);
    }
    const std::vector<std::string> copies = std::move(drag_copies_);
    end_drag();
    if (copies.empty()) {
        return;
    }
    for (const std::string& name : copies) {
        try {
            state_.doc.remove_child(state_.current_group, name);
        } catch (const std::exception& e) {
            callbacks_.error(QString::fromStdString(std::string("cancel copy: ") + e.what()));
        }
    }
    notify_changed();
}

void canvas_scene::end_drag() {
    set_drop_target(nullptr);
    moving_.clear();
    moving_node_ = nullptr;
    pressed_node_ = nullptr;
    copy_pending_ = false;
    drag_copies_.clear();
}

node_item* canvas_scene::node_by_name(const std::string& name) const {
    for (QGraphicsItem* item : items()) {
        if (auto* node = qgraphicsitem_cast<node_item*>(item); node != nullptr && node->child_name() == name) {
            return node;
        }
    }
    return nullptr;
}

void canvas_scene::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    if (temp_link_ != nullptr) {
        temp_link_->setLine(QLineF(drag_from_->scenePos(), event->scenePos()));
        event->accept();
        return;
    }
    if (!moving_.empty()) {
        const QPointF delta = event->scenePos() - press_pos_;
        if (copy_pending_) {
            if (delta.isNull()) {
                event->accept();
                return;
            }
            begin_copy_drag();
            if (moving_.empty()) {
                return;
            }
        }
        for (const dragged& d : moving_) {
            d.item->setPos(d.start + delta);
        }
        set_drop_target(group_node_at(event->scenePos()));
        event->accept();
        return;
    }
    QGraphicsScene::mouseMoveEvent(event);
}

void canvas_scene::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if (temp_link_ != nullptr) {
        removeItem(temp_link_);
        delete temp_link_;
        temp_link_ = nullptr;
        clear_pin_eligibility();
        pin_item* target = pin_at(event->scenePos());
        if (target != nullptr && target != drag_from_ && target->is_output() != drag_from_->is_output()) {
            const std::string& from = drag_from_->is_output() ? drag_from_->port_path() : target->port_path();
            const std::string& to = drag_from_->is_output() ? target->port_path() : drag_from_->port_path();
            try {
                connect_ports(state_.doc, state_.current_group, from, to, describer());
                callbacks_.project_changed();
            } catch (const std::exception& e) {
                callbacks_.error(QString::fromStdString(std::string("connect: ") + e.what()));
            }
        } else if (target == nullptr) {
            try {
                (void)expose_port(state_.doc, state_.current_group, drag_from_->port_path(), drag_from_->is_output());
                callbacks_.project_changed();
            } catch (const std::exception& e) {
                callbacks_.error(QString::fromStdString(std::string("expose: ") + e.what()));
            }
        }
        drag_from_ = nullptr;
        event->accept();
        return;
    }
    if (copy_pending_) {
        pressed_node_->setSelected(!pressed_node_->isSelected());
        end_drag();
        event->accept();
        return;
    }
    if (!moving_.empty()) {
        std::set<std::string> dragged_names;
        for (const dragged& d : moving_) {
            dragged_names.insert(d.item->child_name());
        }
        const std::string into = drop_target_ != nullptr ? drop_target_->child_name() : std::string();
        const std::vector<std::string> names = selected_node_names();
        end_drag();
        QGraphicsScene::mouseReleaseEvent(event);

        if (into.empty()) {
            return;
        }
        for (const std::string& name : names) {
            if (dragged_names.contains(name)) {
                move_into_group(name, into);
            }
        }
        notify_changed();
        return;
    }
    QGraphicsScene::mouseReleaseEvent(event);
}

node_item* canvas_scene::node_at(const QPointF& pos) const {
    for (QGraphicsItem* item : items(pos)) {
        if (auto* node = qgraphicsitem_cast<node_item*>(item)) {
            return node;
        }
    }
    return nullptr;
}

node_item* canvas_scene::group_node_at(const QPointF& pos) const {
    for (QGraphicsItem* item : items(pos)) {
        if (auto* node = qgraphicsitem_cast<node_item*>(item)) {
            if (!node->is_group()) {
                continue;
            }
            const bool dragged_along =
                std::ranges::any_of(moving_, [&](const struct dragged& d) { return d.item == node; });
            if (!dragged_along) {
                return node;
            }
        }
    }
    return nullptr;
}

void canvas_scene::set_drop_target(node_item* node) {
    if (drop_target_ == node) {
        return;
    }
    if (drop_target_ != nullptr) {
        drop_target_->set_drop_target(false);
    }
    drop_target_ = node;
    if (drop_target_ != nullptr) {
        drop_target_->set_drop_target(true);
    }
}

void canvas_scene::move_into_group(const std::string& name, const std::string& into) {
    const std::string target = node_ref{state_.current_group, into}.full();
    try {
        const studio::move_result result = state_.doc.move_child(state_.current_group, name, target);
        QString note = QString("moved '%1' into '%2'").arg(QString::fromStdString(name), QString::fromStdString(into));
        if (result.new_name != name) {
            note += QString(" as '%1'").arg(QString::fromStdString(result.new_name));
        }
        if (result.dropped_connections > 0) {
            note += QString("; %1 connection(s) dropped").arg(result.dropped_connections);
        }
        if (result.dropped_exposes > 0) {
            note += QString("; %1 exported port(s) dropped").arg(result.dropped_exposes);
        }
        callbacks_.error(note);
        state_.selected_child = into;
    } catch (const std::exception& e) {
        callbacks_.error(QString::fromStdString(std::string("move: ") + e.what()));
    }
}

void canvas_scene::paste_at(std::optional<node_position> at) {
    std::optional<node_position> where = at;
    if (!where) {
        if (const std::optional<node_position> origin = clipboard_origin()) {
            where = node_position{origin->x + paste_offset, origin->y + paste_offset};
        }
    }
    std::vector<std::string> made = paste_nodes(state_, callbacks_, state_.current_group, where);
    if (made.empty()) {
        return;
    }
    select_after_rebuild_ = std::move(made);
    notify_changed();
}

void canvas_scene::paste_inside(const std::string& child_name) {
    const std::string target = node_ref{state_.current_group, child_name}.full();
    if (paste_nodes(state_, callbacks_, target, std::nullopt).empty()) {
        return;
    }
    notify_changed();
}

void canvas_scene::delete_selection() {
    std::vector<std::size_t> link_indices;
    std::vector<std::pair<bool, std::string>> stubs;
    std::vector<std::string> nodes;
    for (QGraphicsItem* item : selectedItems()) {
        if (auto* link = qgraphicsitem_cast<link_item*>(item)) {
            link_indices.push_back(link->index());
        } else if (auto* stub = qgraphicsitem_cast<stub_item*>(item)) {
            stubs.emplace_back(stub->is_output(), stub->alias());
        } else if (auto* node = qgraphicsitem_cast<node_item*>(item)) {
            nodes.push_back(node->child_name());
        }
    }
    if (link_indices.empty() && stubs.empty() && nodes.empty()) {
        return;
    }
    std::ranges::sort(link_indices, std::ranges::greater{});
    try {
        {
            const project::edit_scope scope(state_.doc);
            for (std::size_t index : link_indices) {
                state_.doc.disconnect(state_.current_group, index);
            }
            for (const auto& [is_output, alias] : stubs) {
                if (is_output) {
                    state_.doc.remove_expose_output(state_.current_group, alias);
                } else {
                    state_.doc.remove_expose_input(state_.current_group, alias);
                }
            }
            state_.doc.remove_children(state_.current_group, nodes);
        }
        state_.selected_child.clear();
        notify_changed();
    } catch (const std::exception& e) {
        callbacks_.error(QString::fromStdString(std::string("delete: ") + e.what()));
    }
}

std::optional<node_position> canvas_scene::clipboard_origin() const {
    std::optional<node_position> origin;
    for (const clip_node& entry : state_.clip.nodes) {
        if (!entry.position) {
            continue;
        }
        origin = origin ? node_position{std::min(origin->x, entry.position->x), std::min(origin->y, entry.position->y)}
                        : *entry.position;
    }
    return origin;
}

std::vector<std::string> canvas_scene::selected_node_names() const {
    std::set<std::string> chosen;
    for (QGraphicsItem* item : selectedItems()) {
        if (auto* node = qgraphicsitem_cast<node_item*>(item)) {
            chosen.insert(node->child_name());
        }
    }
    std::vector<std::string> names;
    const runtime::group_node* g = state_.doc.group_at(state_.current_group);
    if (g == nullptr) {
        return names;
    }
    for (const runtime::child_node& c : g->modules) {
        if (chosen.contains(detail::child_name(c))) {
            names.push_back(detail::child_name(c));
        }
    }
    return names;
}

void canvas_scene::notify_changed() {
    QTimer::singleShot(0, this, [this] { callbacks_.project_changed(); });
}

void canvas_scene::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) {
    for (QGraphicsItem* item : items(event->scenePos())) {
        if (auto* node = qgraphicsitem_cast<node_item*>(item)) {
            if (node->is_group()) {
                state_.current_group = node_ref{state_.current_group, node->child_name()}.full();
                state_.selected_child.clear();
                callbacks_.project_changed();
                event->accept();
                return;
            }
        }
    }
    QGraphicsScene::mouseDoubleClickEvent(event);
}

void canvas_scene::contextMenuEvent(QGraphicsSceneContextMenuEvent* event) {
    if (state_.view->running()) {
        QGraphicsScene::contextMenuEvent(event);
        return;
    }
    if (pin_item* pin = pin_at(event->scenePos())) {
        QMenu menu;
        QAction* expose = menu.addAction(QStringLiteral("Expose outside"));
        if (menu.exec(event->screenPos()) == expose) {
            try {
                (void)expose_port(state_.doc, state_.current_group, pin->port_path(), pin->is_output());
                callbacks_.project_changed();
            } catch (const std::exception& e) {
                callbacks_.error(QString::fromStdString(std::string("expose: ") + e.what()));
            }
        }
        event->accept();
        return;
    }
    const node_position where{static_cast<float>(event->scenePos().x()), static_cast<float>(event->scenePos().y())};
    if (node_item* node = node_at(event->scenePos())) {
        if (!node->isSelected()) {
            clearSelection();
            node->setSelected(true);
        }
        show_node_menu(event->screenPos(), where, node->child_name(), node->is_group());
        event->accept();
        return;
    }
    if (!items(event->scenePos()).isEmpty()) {
        QGraphicsScene::contextMenuEvent(event);
        return;
    }
    QMenu menu;
    QAction* group = menu.addAction(QStringLiteral("New group here"));
    menu.addSeparator();
    QAction* paste = menu.addAction(QStringLiteral("Paste"));
    paste->setShortcut(QKeySequence::Paste);
    paste->setEnabled(!state_.clip.empty());
    QAction* chosen = menu.exec(event->screenPos());
    if (chosen == group) {
        create_group(state_, callbacks_, where);
    } else if (chosen == paste) {
        paste_at(where);
    }
    event->accept();
}

void canvas_scene::show_node_menu(const QPoint& screen_pos,
                                  node_position where,
                                  const std::string& name,
                                  bool is_group) {
    const std::vector<std::string> names = selected_node_names();
    const std::string into = is_group ? name : std::string();

    QMenu menu;
    QAction* cut = menu.addAction(QStringLiteral("Cut"));
    cut->setShortcut(QKeySequence::Cut);
    QAction* copy = menu.addAction(QStringLiteral("Copy"));
    copy->setShortcut(QKeySequence::Copy);
    QAction* paste = menu.addAction(into.empty() ? QStringLiteral("Paste")
                                                 : QString("Paste into '%1'").arg(QString::fromStdString(into)));
    paste->setShortcut(QKeySequence::Paste);
    paste->setEnabled(!state_.clip.empty());
    QAction* copy_props = nullptr;
    QAction* paste_props = nullptr;
    if (!is_group) {
        menu.addSeparator();
        copy_props = menu.addAction(QStringLiteral("Copy properties"));
        paste_props = menu.addAction(QStringLiteral("Paste properties"));
        paste_props->setEnabled(!state_.clip_properties.empty());
    }
    menu.addSeparator();
    QAction* remove = menu.addAction(QStringLiteral("Delete"));
    remove->setShortcut(QKeySequence::Delete);
    for (QAction* action : {cut, copy, remove}) {
        action->setEnabled(!names.empty());
    }

    QAction* chosen = menu.exec(screen_pos);
    if (chosen != nullptr && chosen == copy_props) {
        (void)copy_properties(state_, callbacks_, state_.current_group, name);
        return;
    }
    if (chosen != nullptr && chosen == paste_props) {
        if (paste_properties(state_, callbacks_, state_.current_group, name)) {
            notify_changed();
        }
        return;
    }
    if (chosen == cut) {
        if (cut_nodes(state_, callbacks_, state_.current_group, names)) {
            notify_changed();
        }
    } else if (chosen == copy) {
        (void)copy_nodes(state_, callbacks_, state_.current_group, names);
    } else if (chosen == paste) {
        if (into.empty()) {
            paste_at(where);
        } else {
            paste_inside(into);
        }
    } else if (chosen == remove) {
        delete_selection();
    }
}

void canvas_scene::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape && (!moving_.empty() || copy_pending_)) {
        cancel_drag();
        event->accept();
        return;
    }
    if (!state_.view->running() && event->matches(QKeySequence::Cut)) {
        if (cut_nodes(state_, callbacks_, state_.current_group, selected_node_names())) {
            notify_changed();
        }
        event->accept();
        return;
    }
    if (!state_.view->running() && event->matches(QKeySequence::Copy)) {
        (void)copy_nodes(state_, callbacks_, state_.current_group, selected_node_names());
        event->accept();
        return;
    }
    if (!state_.view->running() && event->matches(QKeySequence::Paste)) {
        paste_at(std::nullopt);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Delete && !state_.view->running()) {
        delete_selection();
        event->accept();
        return;
    }
    QGraphicsScene::keyPressEvent(event);
}

void canvas_scene::accept_palette_drag(QGraphicsSceneDragDropEvent* event) {
    const QMimeData* mime = event->mimeData();
    if (!state_.view->running() && mime != nullptr && (mime->hasFormat(module_mime_type) || is_group_mime(mime))) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

std::string canvas_scene::pin_key(const std::string& port_path, bool output) {
    return (output ? "o:" : "i:") + port_path;
}

void canvas_scene::mark_eligible_pins(const pin_item& from) {
    for (pin_item* pin : std::views::values(pins_)) {
        if (pin == &from) {
            continue;
        }
        pin->set_eligible(pin->is_output() != from.is_output() && drop_allowed(from, *pin));
    }
}

void canvas_scene::clear_pin_eligibility() {
    for (pin_item* pin : std::views::values(pins_)) {
        pin->set_eligible(true);
    }
}

pin_item* canvas_scene::pin_at(const QPointF& pos) {
    for (QGraphicsItem* item : items(pos)) {
        if (auto* pin = qgraphicsitem_cast<pin_item*>(item)) {
            return pin;
        }
    }
    return nullptr;
}

describe_fn canvas_scene::describer() {
    return [this](const std::string& factory, const std::optional<version>& ver) {
        return state_.describe_cached(factory, ver);
    };
}

void canvas_scene::build_node(const runtime::child_node& c,
                              const std::unordered_map<std::string, node_position>& fallback) {
    const std::string& name = c.module ? c.module->name : c.group->name;
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

    const double height = node_header + (pin_row * static_cast<double>(ports.size())) + 6.0;
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
        auto* pin = new pin_item(node, name + "." + port, is_output, port_type);
        pin->setToolTip(pin_tooltip(port, port_type, is_output));
        pin->setPos(is_output ? node_width : 0.0, y + 7.0);
        pins_[pin_key(name + "." + port, is_output)] = pin;
        y += pin_row;
    }

    const std::string full = node_ref{state_.current_group, name}.full();
    std::optional<node_position> p = state_.doc.position(full);
    if (!p) {
        p = fallback.at(name);
        state_.doc.set_position(full, *p);
    }
    node->setPos(p->x, p->y);
    node->on_moved = [this, full](node_item& moved) {
        state_.doc.set_position(full, {static_cast<float>(moved.pos().x()), static_cast<float>(moved.pos().y())});
        update_link_paths();
    };
}

void canvas_scene::rebuild_links(const runtime::group_node& g) {
    for (std::size_t i = 0; i < g.connections.size(); ++i) {
        auto* link = new link_item(i);
        addItem(link);
        links_.push_back(link);
    }
    update_link_paths();
}

void canvas_scene::build_stubs(const runtime::group_node& g) {
    for (const auto& [alias, path] : g.expose_inputs) {
        add_stub(g, alias, path, false);
    }
    for (const auto& [alias, path] : g.expose_outputs) {
        add_stub(g, alias, path, true);
    }
    reposition_stubs();
}

void canvas_scene::add_stub(const runtime::group_node& g,
                            const std::string& alias,
                            const std::string& path,
                            bool is_output) {
    auto pin = pins_.find(pin_key(path, is_output));
    if (pin == pins_.end()) {
        return;
    }
    const std::optional<std::type_index> port_type = resolve_port_type(g, path, is_output, describer());
    auto* stub = new stub_item(is_output, alias, port_type);
    addItem(stub);
    stubs_.push_back(stub);
}

void canvas_scene::reposition_stubs() {
    const runtime::group_node* g = state_.doc.group_at(state_.current_group);
    if (g == nullptr) {
        return;
    }
    for (stub_item* stub : stubs_) {
        const auto& map = stub->is_output() ? g->expose_outputs : g->expose_inputs;
        auto entry = std::ranges::find_if(map, [&](const auto& e) { return e.first == stub->alias(); });
        if (entry == map.end()) {
            continue;
        }
        auto pin = pins_.find(pin_key(entry->second, stub->is_output()));
        if (pin != pins_.end()) {
            stub->set_anchor(pin->second->scenePos());
        }
    }
}

}  // namespace atp::studio::ui
