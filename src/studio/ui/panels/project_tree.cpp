// SPDX-License-Identifier: Apache-2.0
#include "panels/project_tree.hpp"

#include "model/clipboard_actions.hpp"
#include "model/drag_payloads.hpp"
#include "model/property_actions.hpp"

#include <exception>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <QAction>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QHeaderView>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMenu>
#include <QMimeData>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QRect>
#include <QTimer>

#include <atp/studio/add_group.hpp>
#include <atp/studio/add_module.hpp>
#include <atp/studio/node_ref.hpp>

namespace atp::studio::ui {
namespace {

/// Pen the drop frame is drawn with, and how round its corners are. Two pixels, so that the frame
/// reads as a frame rather than as an artefact of the row separator underneath it.
constexpr int drop_frame_width = 2;
constexpr qreal drop_frame_radius = 3.0;

void collect_state(const QTreeWidgetItem* item, int role, std::set<QString>& known, std::set<QString>& expanded) {
    const QString path = item->data(0, role).toString();
    known.insert(path);
    if (item->isExpanded()) {
        expanded.insert(path);
    }
    for (int i = 0; i < item->childCount(); ++i) {
        collect_state(item->child(i), role, known, expanded);
    }
}

void restore_expanded(QTreeWidgetItem* item,
                      int role,
                      const std::set<QString>& known,
                      const std::set<QString>& expanded) {
    const QString path = item->data(0, role).toString();
    item->setExpanded(expanded.contains(path) || !known.contains(path));
    for (int i = 0; i < item->childCount(); ++i) {
        restore_expanded(item->child(i), role, known, expanded);
    }
}

void append_signature(const runtime::group_node& g, std::string& out) {
    for (const runtime::child_node& c : g.modules) {
        if (c.module) {
            out += "m:" + c.module->name + "@" + c.module->factory + "@" +
                   (c.module->factory_version ? c.module->factory_version->to_string() : "latest") + ";";
        } else {
            out += "g:" + c.group->name + "{";
            append_signature(*c.group, out);
            out += "};";
        }
    }
}

QTreeWidgetItem* find_by_path(QTreeWidgetItem* item, int role, const QString& path) {
    if (item->data(0, role).toString() == path) {
        return item;
    }
    for (int i = 0; i < item->childCount(); ++i) {
        if (QTreeWidgetItem* hit = find_by_path(item->child(i), role, path)) {
            return hit;
        }
    }
    return nullptr;
}

}  // namespace

project_tree::project_tree(app_state& state, ui_callbacks& callbacks, QWidget* parent)
    : QTreeWidget(parent), state_(state), callbacks_(callbacks) {
    setHeaderLabels({"node", "factory"});
    setToolTip("drag to move between groups; F2 renames");
    style::embed_tree(this);
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(0, QHeaderView::Stretch);
    header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::MoveAction);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::DoubleClicked);
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
    setDropIndicatorShown(false);
    QObject::connect(this, &QTreeWidget::currentItemChanged, this,
                     [this](QTreeWidgetItem*, QTreeWidgetItem*) { selection_moved(); });
    QObject::connect(this, &QTreeWidget::itemSelectionChanged, this, [this] { selection_moved(); });
    QObject::connect(this, &QTreeWidget::itemChanged, this,
                     [this](QTreeWidgetItem* item, int) { commit_rename(item); });
}

void project_tree::refresh() {
    rebuilding_ = true;
    std::string signature;
    append_signature(state_.doc.config().pipeline, signature);

    if (signature != signature_) {
        std::set<QString> known;
        std::set<QString> expanded;
        for (int i = 0; i < topLevelItemCount(); ++i) {
            collect_state(topLevelItem(i), path_role, known, expanded);
        }
        clear();

        auto* fresh = new QTreeWidgetItem(this);
        fresh->setText(0, "root");
        fresh->setIcon(0, group_icon_);
        fresh->setData(0, path_role, QString());
        fresh->setData(0, group_role, true);
        build_children(fresh, state_.doc.config().pipeline, "");

        restore_expanded(fresh, path_role, known, expanded);
        fresh->setExpanded(true);
        signature_ = signature;
    }
    QTreeWidgetItem* root = topLevelItem(0);

    const std::string wanted = node_ref{state_.current_group, state_.selected_child}.full();
    if (QTreeWidgetItem* item =
            root == nullptr ? nullptr : find_by_path(root, path_role, QString::fromStdString(wanted))) {
        setCurrentItem(item);
        item->setSelected(true);
        scrollToItem(item);
    }
    setEnabled(!state_.view->running());
    rebuilding_ = false;
}

void project_tree::build_children(QTreeWidgetItem* parent, const runtime::group_node& g, const std::string& path) {
    for (const runtime::child_node& c : g.modules) {
        const std::string& name = c.module ? c.module->name : c.group->name;
        const std::string full = node_ref{path, name}.full();
        auto* item = new QTreeWidgetItem(parent);
        item->setText(0, QString::fromStdString(name));
        item->setData(0, path_role, QString::fromStdString(full));
        item->setData(0, group_role, !c.module.has_value());
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        if (c.module) {
            item->setIcon(0, module_icon_);
            item->setText(1, QString::fromStdString(c.module->factory));
            item->setToolTip(1, QString::fromStdString(
                                    c.module->factory + "@" +
                                    (c.module->factory_version ? c.module->factory_version->to_string() : "latest")));
        } else {
            item->setIcon(0, group_icon_);
            build_children(item, *c.group, full);
        }
    }
}

QStringList project_tree::mimeTypes() const {
    return {QString::fromLatin1(node_mime_type), QString::fromLatin1(module_mime_type),
            QString::fromLatin1(group_mime_type)};
}

QMimeData* project_tree::mimeData(const QList<QTreeWidgetItem*>& items) const {
    if (items.isEmpty() || state_.view->running()) {
        return nullptr;
    }
    const QString path = items.front()->data(0, path_role).toString();
    if (path.isEmpty()) {
        return nullptr;
    }
    const node_ref moved = node_ref::parse(path.toStdString());
    return encode_node_mime({QString::fromStdString(moved.group), QString::fromStdString(moved.name)});
}

void project_tree::dragEnterEvent(QDragEnterEvent* event) {
    accept_drag(event);
}

void project_tree::dragMoveEvent(QDragMoveEvent* event) {
    accept_drag(event);
}

void project_tree::dragLeaveEvent(QDragLeaveEvent* event) {
    clear_drop_target();
    QTreeWidget::dragLeaveEvent(event);
}

void project_tree::accept_drag(QDragMoveEvent* event) {
    const QMimeData* mime = event->mimeData();
    if (state_.view->running() || mime == nullptr ||
        !(mime->hasFormat(node_mime_type) || mime->hasFormat(module_mime_type) || is_group_mime(mime))) {
        clear_drop_target();
        event->ignore();
        return;
    }
    const std::string target = drop_group(event->position().toPoint());

    node_mime_payload node;
    if (decode_node_mime(mime, node) &&
        !move_allowed({node.group_path.toStdString(), node.name.toStdString()}, target)) {
        clear_drop_target();
        event->ignore();
        return;
    }
    set_drop_target(QString::fromStdString(target));
    event->acceptProposedAction();
}

bool project_tree::move_allowed(const node_ref& what, const std::string& target) {
    if (what.group == target) {
        return false;
    }
    return !what.contains(target);
}

void project_tree::clear_drop_target() {
    set_drop_target(std::nullopt);
}

void project_tree::set_drop_target(std::optional<QString> path) {
    if (drop_path_ == path) {
        return;
    }
    drop_path_ = std::move(path);
    viewport()->update();
}

void project_tree::paintEvent(QPaintEvent* event) {
    QTreeWidget::paintEvent(event);
    if (!drop_path_) {
        return;
    }
    QTreeWidgetItem* root = topLevelItem(0);
    QTreeWidgetItem* target = root == nullptr ? nullptr : find_by_path(root, path_role, *drop_path_);
    if (target == nullptr) {
        return;
    }
    QRect row = visualItemRect(target);
    if (row.isEmpty()) {
        return;
    }
    row.setLeft(0);
    row.setRight(viewport()->width() - 1);

    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(palette().color(QPalette::Highlight), drop_frame_width));
    painter.setBrush(Qt::NoBrush);
    const qreal inset = drop_frame_width / 2.0;
    painter.drawRoundedRect(QRectF(row).adjusted(inset, inset, -inset, -inset), drop_frame_radius, drop_frame_radius);
}

void project_tree::dropEvent(QDropEvent* event) {
    clear_drop_target();
    if (state_.view->running()) {
        event->ignore();
        return;
    }
    const std::string target = drop_group(event->position().toPoint());
    const QMimeData* mime = event->mimeData();

    if (is_group_mime(mime)) {
        add_group_to(target);
        event->acceptProposedAction();
        return;
    }

    node_mime_payload node;
    if (decode_node_mime(mime, node)) {
        const std::string from = node.group_path.toStdString();
        const std::string name = node.name.toStdString();
        if (from == target) {
            event->ignore();
            return;
        }
        try {
            const studio::move_result result = state_.doc.move_child(from, name, target);
            const QString into = target.empty() ? QString("root") : QString::fromStdString(target);
            QString note = QString("moved '%1' into '%2'").arg(QString::fromStdString(name), into);
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
            state_.current_group = target;
            state_.selected_child = result.new_name;
            notify_changed();
        } catch (const std::exception& e) {
            callbacks_.error(QString::fromStdString(std::string("move: ") + e.what()));
        }
        event->acceptProposedAction();
        return;
    }

    module_mime_payload payload;
    if (decode_module_mime(mime, payload)) {
        studio::add_module_request request;
        request.group_path = target;
        request.factory = payload.factory.toStdString();
        request.factory_version = try_parse_version(payload.version.toStdString());
        request.plugin = std::filesystem::path(payload.plugin.toStdWString());
        request.config_dir = state_.config_dir();
        try {
            const studio::add_module_result result = studio::add_module(state_.doc, request);
            if (!result.warning.empty()) {
                callbacks_.error(QString::fromStdString("warning: " + result.warning));
            }
            state_.current_group = target;
            state_.selected_child = result.name;
            notify_changed();
        } catch (const std::exception& e) {
            callbacks_.error(QString::fromStdString(std::string("add module: ") + e.what()));
        }
        event->acceptProposedAction();
        return;
    }
    event->ignore();
}

std::string project_tree::drop_group(const QPoint& pos) const {
    return paste_target(itemAt(pos));
}

std::string project_tree::paste_target(const QTreeWidgetItem* item) const {
    if (item == nullptr) {
        return {};
    }
    if (item->data(0, group_role).toBool()) {
        return item->data(0, path_role).toString().toStdString();
    }
    const QTreeWidgetItem* parent = item->parent();
    return parent == nullptr ? std::string() : parent->data(0, path_role).toString().toStdString();
}

void project_tree::paste_into(const std::string& group) {
    const std::vector<std::string> made = paste_nodes(state_, callbacks_, group, std::nullopt);
    if (made.empty()) {
        return;
    }
    state_.current_group = group;
    state_.selected_child = made.back();
    notify_changed();
}

void project_tree::selection_moved() {
    if (rebuilding_) {
        return;
    }
    const QList<QTreeWidgetItem*> chosen = selectedItems();
    const QTreeWidgetItem* item = chosen.isEmpty() ? currentItem() : chosen.front();
    if (item == nullptr) {
        return;
    }
    const node_ref landed = node_ref::parse(item->data(0, path_role).toString().toStdString());
    if (landed.group == state_.current_group && landed.name == state_.selected_child) {
        return;
    }
    state_.current_group = landed.group;
    state_.selected_child = landed.name;
    notify_changed();
}

void project_tree::commit_rename(QTreeWidgetItem* item) {
    if (rebuilding_ || item == nullptr) {
        return;
    }
    const std::string full = item->data(0, path_role).toString().toStdString();
    if (full.empty()) {
        return;
    }
    const node_ref renamed = node_ref::parse(full);
    const std::string& old_name = renamed.name;
    const std::string next = item->text(0).toStdString();
    if (next == old_name) {
        return;
    }
    try {
        state_.doc.rename_child(renamed.group, old_name, next);
    } catch (const std::exception& e) {
        rebuilding_ = true;
        item->setText(0, QString::fromStdString(old_name));
        rebuilding_ = false;
        callbacks_.error(QString::fromStdString(std::string("rename: ") + e.what()));
        return;
    }
    state_.current_group = renamed.group;
    state_.selected_child = next;
    notify_changed();
}

bool project_tree::viewportEvent(QEvent* event) {
    if (event->type() == QEvent::ContextMenu) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
        auto* menu_event = static_cast<QContextMenuEvent*>(event);
        show_menu(menu_event->pos(), menu_event->globalPos());
        menu_event->accept();
        return true;
    }
    return QTreeWidget::viewportEvent(event);
}

void project_tree::show_menu(const QPoint& pos, const QPoint& global) {
    if (state_.view->running()) {
        return;
    }
    QTreeWidgetItem* item = itemAt(pos);
    if (item == nullptr) {
        return;
    }
    setCurrentItem(item);
    const std::string full = item->data(0, path_role).toString().toStdString();
    const bool is_root = full.empty();
    const node_ref picked = node_ref::parse(full);

    QMenu menu;
    QAction* new_group = menu.addAction(QStringLiteral("New group"));
    menu.addSeparator();
    QAction* cut = menu.addAction(QStringLiteral("Cut"));
    cut->setShortcut(QKeySequence::Cut);
    cut->setEnabled(!is_root);
    QAction* copy = menu.addAction(QStringLiteral("Copy"));
    copy->setShortcut(QKeySequence::Copy);
    copy->setEnabled(!is_root);
    QAction* paste = menu.addAction(QStringLiteral("Paste"));
    paste->setShortcut(QKeySequence::Paste);
    paste->setEnabled(!state_.clip.empty());
    const bool is_module = !is_root && !item->data(0, group_role).toBool();
    QAction* copy_props = nullptr;
    QAction* paste_props = nullptr;
    if (is_module) {
        menu.addSeparator();
        copy_props = menu.addAction(QStringLiteral("Copy properties"));
        paste_props = menu.addAction(QStringLiteral("Paste properties"));
        paste_props->setEnabled(!state_.clip_properties.empty());
    }
    menu.addSeparator();
    QAction* rename = is_root ? nullptr : menu.addAction(QStringLiteral("Rename"));
    QAction* remove = is_root ? nullptr : menu.addAction(QStringLiteral("Delete"));

    QAction* chosen = menu.exec(global);
    if (chosen == nullptr) {
        return;
    }
    if (chosen == copy_props) {
        (void)copy_properties(state_, callbacks_, picked.group, picked.name);
        return;
    }
    if (chosen == paste_props) {
        if (paste_properties(state_, callbacks_, picked.group, picked.name)) {
            notify_changed();
        }
        return;
    }
    if (chosen == new_group) {
        add_group_to(paste_target(item));
    } else if (chosen == cut) {
        if (cut_nodes(state_, callbacks_, picked.group, {picked.name})) {
            notify_changed();
        }
    } else if (chosen == copy) {
        (void)copy_nodes(state_, callbacks_, picked.group, {picked.name});
    } else if (chosen == paste) {
        paste_into(paste_target(item));
    } else if (chosen == rename) {
        editItem(item, 0);
    } else if (chosen == remove) {
        remove_current();
    }
}

void project_tree::add_group_to(const std::string& group) {
    try {
        state_.selected_child = studio::add_group(state_.doc, group);
        state_.current_group = group;
        notify_changed();
    } catch (const std::exception& e) {
        callbacks_.error(QString::fromStdString(std::string("add group: ") + e.what()));
    }
}

void project_tree::remove_current() {
    QTreeWidgetItem* item = currentItem();
    if (item == nullptr) {
        return;
    }
    const std::string full = item->data(0, path_role).toString().toStdString();
    if (full.empty()) {
        return;
    }
    const node_ref doomed = node_ref::parse(full);
    try {
        state_.doc.remove_child(doomed.group, doomed.name);
        state_.current_group = doomed.group;
        state_.selected_child.clear();
        notify_changed();
    } catch (const std::exception& e) {
        callbacks_.error(QString::fromStdString(std::string("delete: ") + e.what()));
    }
}

void project_tree::keyPressEvent(QKeyEvent* event) {
    if (state_.view->running()) {
        QTreeWidget::keyPressEvent(event);
        return;
    }
    if (event->key() == Qt::Key_Delete) {
        remove_current();
        event->accept();
        return;
    }
    const QTreeWidgetItem* item = currentItem();
    const std::string full = item == nullptr ? std::string() : item->data(0, path_role).toString().toStdString();
    if (event->matches(QKeySequence::Cut) || event->matches(QKeySequence::Copy)) {
        if (!full.empty()) {
            const node_ref picked = node_ref::parse(full);
            if (event->matches(QKeySequence::Cut)) {
                if (cut_nodes(state_, callbacks_, picked.group, {picked.name})) {
                    notify_changed();
                }
            } else {
                (void)copy_nodes(state_, callbacks_, picked.group, {picked.name});
            }
        }
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Paste)) {
        paste_into(paste_target(item));
        event->accept();
        return;
    }
    QTreeWidget::keyPressEvent(event);
}

void project_tree::notify_changed() {
    QTimer::singleShot(0, this, [this] { callbacks_.project_changed(); });
}

}  // namespace atp::studio::ui
