// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_UI_PROJECT_TREE_HPP
#define ATP_STUDIO_UI_PROJECT_TREE_HPP

#include "kit/icons.hpp"
#include "kit/ui_style.hpp"
#include "model/app_state.hpp"

#include <optional>
#include <string>

#include <QIcon>
#include <QPoint>
#include <QString>
#include <QTreeWidget>

#include <atp/studio/node_ref.hpp>

namespace atp::studio::ui {

/// Tree of the whole project: every group and every module in it. It is the only view that shows
/// more than one group at a time — the canvas shows the one it is inside — and the place where
/// nodes are moved between groups, renamed and removed. Selecting a row moves the canvas to the
/// node's group and selects it there, so the two views always show the same node.
class project_tree final : public QTreeWidget {
   public:
    project_tree(app_state& state, ui_callbacks& callbacks, QWidget* parent = nullptr);

    /// Rebuilds the tree from the project, restoring what was expanded and putting the cursor back
    /// on the node the application state points at.
    void refresh();

   protected:
    [[nodiscard]] QStringList mimeTypes() const override;

    [[nodiscard]] QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override;

    void dragEnterEvent(QDragEnterEvent* event) override;

    void dragMoveEvent(QDragMoveEvent* event) override;

    void dragLeaveEvent(QDragLeaveEvent* event) override;

    void dropEvent(QDropEvent* event) override;

    void paintEvent(QPaintEvent* event) override;

    void keyPressEvent(QKeyEvent* event) override;

    bool viewportEvent(QEvent* event) override;

   private:
    static constexpr int path_role = Qt::UserRole;
    static constexpr int group_role = Qt::UserRole + 1;

    void build_children(QTreeWidgetItem* parent, const runtime::group_node& g, const std::string& path);

    [[nodiscard]] std::string drop_group(const QPoint& pos) const;

    /// Group a paste lands in for a row: the group itself, or the parent of a module. Same rule the
    /// drop target follows, which is why drop_group defers to it.
    [[nodiscard]] std::string paste_target(const QTreeWidgetItem* item) const;

    /// Pastes the clipboard into a group and puts the selection on what arrived.
    void paste_into(const std::string& group);

    void selection_moved();

    void commit_rename(QTreeWidgetItem* item);

    void show_menu(const QPoint& pos, const QPoint& global);

    void add_group_to(const std::string& group);

    void remove_current();

    void accept_drag(QDragMoveEvent* event);

    [[nodiscard]] static bool move_allowed(const node_ref& what, const std::string& target);

    void clear_drop_target();

    void set_drop_target(std::optional<QString> path);

    void notify_changed();

    app_state& state_;
    ui_callbacks& callbacks_;
    std::string signature_;
    bool rebuilding_ = false;

    std::optional<QString> drop_path_;

    QIcon group_icon_ = icons::group();
    icons::module_icons module_icons_;
};

}  // namespace atp::studio::ui

#endif
