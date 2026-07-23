#include "palette_widget.hpp"

#include "module_mime.hpp"

#include <filesystem>
#include <string>

namespace atp::studio::ui {

palette_widget::palette_widget(app_state& state, ui_callbacks& callbacks, QWidget* parent)
    : QTreeWidget(parent), state_(state), callbacks_(callbacks) {
    setHeaderLabel("modules (double-click or drag to add)");
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragOnly);
    QObject::connect(this, &QTreeWidget::itemDoubleClicked, this,
                     [this](QTreeWidgetItem* item, int) { add_from_item(item); });
}

void palette_widget::refresh() {
    clear();
    for (const plugin_info& p : state_.manager.plugins()) {
        if (!p.loaded) {
            continue;  // отказы видны в панели Modules
        }
        auto* root = new QTreeWidgetItem(this);
        root->setText(0, QString::fromStdWString(p.path.filename().wstring()));
        for (const module_info& m : p.modules) {
            auto* item = new QTreeWidgetItem(root);
            item->setText(0, QString::fromStdString(m.name + " " + m.ver.to_string()));
            item->setData(0, Qt::UserRole, QString::fromStdString(m.name));
            item->setData(0, Qt::UserRole + 1, QString::fromStdString(m.ver.to_string()));
            item->setData(0, Qt::UserRole + 2, QString::fromStdWString(p.path.wstring()));
            if (m.broken) {
                item->setDisabled(true);
                item->setToolTip(0, QString::fromStdString(m.error));
            }
        }
    }
    expandAll();
    setEnabled(!state_.run.running());  // документ read-only на ходу
}

QStringList palette_widget::mimeTypes() const {
    return {QString::fromLatin1(module_mime_type)};
}

QMimeData* palette_widget::mimeData(const QList<QTreeWidgetItem*>& items) const {
    if (items.size() != 1 || state_.run.running()) {
        return nullptr;
    }
    QTreeWidgetItem* item = items.front();
    if (item->parent() == nullptr || item->isDisabled()) {
        return nullptr;
    }
    return encode_module_mime({item->data(0, Qt::UserRole).toString(),
                               item->data(0, Qt::UserRole + 1).toString(),
                               item->data(0, Qt::UserRole + 2).toString()});
}

void palette_widget::add_from_item(QTreeWidgetItem* item) {
    if (item == nullptr || item->parent() == nullptr || item->isDisabled() || state_.run.running()) {
        return;  // даблклик по заголовку плагина — не добавление
    }
    add_module_from_item(item, std::nullopt);
}

void palette_widget::add_module_from_item(QTreeWidgetItem* item, std::optional<node_position> position) {
    studio::add_module_request request;
    request.group_path = state_.current_group;
    request.factory = item->data(0, Qt::UserRole).toString().toStdString();
    request.factory_version = try_parse_version(item->data(0, Qt::UserRole + 1).toString().toStdString());
    request.plugin = std::filesystem::path(item->data(0, Qt::UserRole + 2).toString().toStdWString());
    request.config_dir = state_.config_dir();
    request.position = position;
    try {
        const studio::add_module_result result = studio::add_module(state_.doc, request);
        if (!result.warning.empty()) {
            callbacks_.error(QString::fromStdString("warning: " + result.warning));
        }
        callbacks_.document_changed();
    } catch (const std::exception& e) {
        callbacks_.error(QString::fromStdString(std::string("add module: ") + e.what()));
    }
}

}  // namespace atp::studio::ui
