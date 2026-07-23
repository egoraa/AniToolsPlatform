#include "manager_widget.hpp"

#include <cstddef>
#include <filesystem>

#include <QBrush>
#include <QColor>
#include <QFileDialog>
#include <QPushButton>
#include <QVBoxLayout>

namespace atp::studio::ui {

manager_widget::manager_widget(app_state& state, ui_callbacks& callbacks, QWidget* parent)
    : QWidget(parent), state_(state), callbacks_(callbacks) {
    auto* layout = new QVBoxLayout(this);
    auto* add = new QPushButton("Add search directory...", this);
    auto* remove = new QPushButton("Remove selected", this);
    auto* rescan = new QPushButton("Rescan", this);
    dirs_ = new QListWidget(this);
    plugins_ = new QTreeWidget(this);
    plugins_->setHeaderLabels({"plugin", "status"});
    layout->addWidget(add);
    layout->addWidget(dirs_);
    layout->addWidget(remove);
    layout->addWidget(rescan);
    layout->addWidget(plugins_);

    QObject::connect(add, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, "Module search directory");
        if (dir.isEmpty()) {
            return;
        }
        state_.manager.add_search_dir(std::filesystem::path(dir.toStdWString()));
        sync_settings();
        state_.manager.rescan();
        state_.describe_cache.clear();
        callbacks_.document_changed();
    });
    QObject::connect(remove, &QPushButton::clicked, this, [this] {
        const int row = dirs_->currentRow();
        if (row < 0) {
            return;
        }
        state_.manager.remove_search_dir(state_.manager.search_dirs()[static_cast<std::size_t>(row)]);
        sync_settings();
        callbacks_.document_changed();
    });
    QObject::connect(rescan, &QPushButton::clicked, this, [this] {
        state_.manager.rescan();
        state_.describe_cache.clear();
        callbacks_.document_changed();
    });
}

void manager_widget::refresh() {
    dirs_->clear();
    for (const auto& dir : state_.manager.search_dirs()) {
        dirs_->addItem(QString::fromStdWString(dir.wstring()));
    }
    plugins_->clear();
    for (const plugin_info& p : state_.manager.plugins()) {
        auto* item = new QTreeWidgetItem(plugins_);
        item->setText(0, QString::fromStdWString(p.path.filename().wstring()));
        if (p.loaded) {
            item->setText(1, "loaded");
            for (const module_info& m : p.modules) {
                auto* child = new QTreeWidgetItem(item);
                child->setText(0, QString::fromStdString(m.name + " " + m.ver.to_string()));
                child->setText(1, m.broken ? "broken" : "ok");
                if (m.broken) {
                    child->setToolTip(1, QString::fromStdString(m.error));
                }
            }
        } else {
            item->setText(1, "failed");
            item->setToolTip(1, QString::fromStdString(p.error));
            item->setForeground(0, QBrush(QColor(220, 80, 80)));
        }
    }
    plugins_->expandAll();
}

void manager_widget::sync_settings() {
    state_.settings.search_dirs.clear();
    for (const auto& d : state_.manager.search_dirs()) {
        state_.settings.search_dirs.push_back(d.string());
    }
    save_settings(state_.settings, state_.settings_file);
}

}  // namespace atp::studio::ui
