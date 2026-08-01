#ifndef ATP_STUDIO_UI_EXPOSE_EDITOR_HPP
#define ATP_STUDIO_UI_EXPOSE_EDITOR_HPP

#include "kit/ui_style.hpp"
#include "model/app_state.hpp"

#include <functional>
#include <string>

#include <QToolButton>
#include <QTreeWidget>
#include <QWidget>

namespace atp::studio::ui {

/// Exported ports of a group in one direction: a two-column table (alias, port) with add and remove
/// buttons under it. The alias is typed, the port is picked from the group's own children — a path
/// that cannot be mistyped is worth more than a free-text field validated after the fact.
class expose_editor final : public QWidget {
   public:
    expose_editor(app_state& state, ui_callbacks& callbacks, bool inputs, QWidget* parent = nullptr);

    /// Rebuilds the table from the group at the given path.
    /// @param group_path path of the group whose exports are shown
    void rebuild(const std::string& group_path);

    /// Re-reads the table when the project changed under it. The rebuild is queued rather than
    /// done on the spot: sync() is reached from an edit in this very table (a cell editor's signal
    /// → project_changed → the window's refresh), and rebuilding destroys that editor — deleting
    /// the sender of the signal being emitted is unsafe in Qt.
    void sync();

   private:
    void add_export();

    void remove_selected();

    void commit_alias(QTreeWidgetItem* item);

    void commit_port(QTreeWidgetItem* item, const std::string& port_path);

    void guard(const char* context, const std::function<void()>& operation);

    app_state& state_;
    ui_callbacks& callbacks_;
    bool inputs_;
    QTreeWidget* tree_ = nullptr;
    QToolButton* add_ = nullptr;
    QToolButton* remove_ = nullptr;
    std::string group_path_;
    bool filling_ = false;
};

}  // namespace atp::studio::ui

#endif
