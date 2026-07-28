#include "manager_widget.hpp"

#include <cstddef>
#include <filesystem>

#include <QBrush>
#include <QColor>
#include <QFileDialog>
#include <QVBoxLayout>

namespace atp::studio::ui {

manager_widget::manager_widget(app_state& state, ui_callbacks& callbacks, QWidget* parent)
    : QWidget(parent), state_(state), callbacks_(callbacks) {
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(style::section_spacing);

    const style::section dirs = style::make_section("search directories", this);
    dirs_ = new QListWidget(dirs.box);
    style::embed_view(dirs_);
    dirs.form->addRow(dirs_);
    const style::button_bar bar = style::make_button_bar(dirs.box);
    auto* add = style::tool_button(style::glyph::add, "add a search directory", bar.box);
    auto* remove = style::tool_button(style::glyph::drop, "drop the selected directory", bar.box);
    auto* rescan = style::tool_button(style::glyph::rescan, "scan the directories again", bar.box);
    bar.row->addWidget(add);
    bar.row->addWidget(remove);
    bar.row->addWidget(rescan);
    bar.row->addStretch(1);
    dirs.form->addRow(bar.box);
    layout->addWidget(dirs.box);

    const style::section found = style::make_section("plugins", this);
    plugins_ = new QTreeWidget(found.box);
    plugins_->setHeaderLabels({"plugin", "status"});
    style::embed_view(plugins_);
    found.form->addRow(plugins_);
    layout->addWidget(found.box, 1);  // the plugin tree is what the panel is for: it gets the height

    QObject::connect(add, &QToolButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, "Module search directory");
        if (dir.isEmpty()) {
            return;
        }
        state_.manager.add_search_dir(std::filesystem::path(dir.toStdWString()));
        sync_settings();
        state_.manager.rescan();
        state_.invalidate_descriptions();
        callbacks_.document_changed();
    });
    QObject::connect(remove, &QToolButton::clicked, this, [this] {
        const int row = dirs_->currentRow();
        if (row < 0) {
            return;
        }
        state_.manager.remove_search_dir(state_.manager.search_dirs()[static_cast<std::size_t>(row)]);
        sync_settings();
        callbacks_.document_changed();
    });
    QObject::connect(rescan, &QToolButton::clicked, this, [this] {
        state_.manager.rescan();
        state_.invalidate_descriptions();
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
