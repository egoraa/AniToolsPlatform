// SPDX-License-Identifier: Apache-2.0
#include "panels/manager_widget.hpp"

#include "model/editor.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

#include <QAction>
#include <QBrush>
#include <QClipboard>
#include <QColor>
#include <QCoreApplication>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHeaderView>
#include <QMenu>
#include <QVBoxLayout>

#include <atp/studio/languages.hpp>
#include <atp/studio/script_modules.hpp>

namespace atp::studio::ui {

manager_widget::manager_widget(app_state& state, ui_callbacks& callbacks, QWidget* parent)
    : QWidget(parent), state_(state), callbacks_(callbacks) {
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(style::section_spacing);

    const style::section dirs = style::make_section("search directories", this);
    dirs_ = new QListWidget(dirs.box);
    style::embed_view(dirs_);
    dirs_->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(dirs_, &QListWidget::customContextMenuRequested, this,
                     [this](const QPoint& pos) { show_dirs_menu(pos, dirs_->viewport()->mapToGlobal(pos)); });
    dirs.form->addRow(dirs_);
    const style::button_bar bar = style::make_button_bar(dirs.box);
    auto* add = style::tool_button(style::glyph::add, "add a search directory", bar.box);
    auto* remove = style::tool_button(style::glyph::drop, "drop the selected directory", bar.box);
    auto* rescan = style::tool_button(style::glyph::rescan, "read the plugins again and scan the directories", bar.box);
    bar.row->addWidget(add);
    bar.row->addWidget(remove);
    bar.row->addWidget(rescan);
    bar.row->addStretch(1);
    dirs.form->addRow(bar.box);
    layout->addWidget(dirs.box);

    const style::section found = style::make_section("found plugins", this);
    plugins_ = new QTreeWidget(found.box);
    plugins_->setHeaderLabels({"plugin", "status"});
    plugins_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    plugins_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    style::embed_tree(plugins_);
    plugins_->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(plugins_, &QTreeWidget::customContextMenuRequested, this,
                     [this](const QPoint& pos) { show_plugin_menu(pos, plugins_->viewport()->mapToGlobal(pos)); });
    found.form->addRow(plugins_);
    layout->addWidget(found.box, 1);

    QObject::connect(add, &QToolButton::clicked, this, [this] { add_dir(); });
    QObject::connect(remove, &QToolButton::clicked, this, [this] { drop_dir(dirs_->currentRow()); });
    QObject::connect(rescan, &QToolButton::clicked, this, [this] { scan(); });
}

void manager_widget::add_dir() {
    const QString dir = QFileDialog::getExistingDirectory(this, "Module search directory");
    if (dir.isEmpty()) {
        return;
    }
    state_.manager.add_search_dir(std::filesystem::path(dir.toStdWString()));
    sync_settings();
    scan();
}

void manager_widget::drop_dir(int row) {
    const std::vector<std::filesystem::path>& dirs = state_.manager.search_dirs();
    if (row < 0 || static_cast<std::size_t>(row) >= dirs.size()) {
        return;
    }
    const std::filesystem::path dir = dirs[static_cast<std::size_t>(row)];
    state_.manager.remove_search_dir(dir);
    sync_settings();
    callbacks_.project_changed();
}

void manager_widget::show_dirs_menu(const QPoint& pos, const QPoint& global) {
    QListWidgetItem* item = dirs_->itemAt(pos);
    if (item != nullptr) {
        dirs_->setCurrentItem(item);
    }

    QMenu menu;
    QAction* add = menu.addAction(QStringLiteral("Add directory"));
    QAction* drop = menu.addAction(QStringLiteral("Drop directory"));
    drop->setEnabled(item != nullptr);
    menu.addSeparator();
    QAction* rescan = menu.addAction(QStringLiteral("Rescan"));
    QAction* chosen = menu.exec(global);
    if (chosen == add) {
        add_dir();
    } else if (chosen == drop) {
        drop_dir(dirs_->row(item));
    } else if (chosen == rescan) {
        scan();
    }
}

void manager_widget::scan() {
    if (state_.view->running()) {
        callbacks_.report(QStringLiteral("plugins already loaded were left as they are: re-reading one would "
                                         "unregister factories the running tree is holding"),
                          atp::log_level::warning);
    } else {
        state_.manager.reload_all();
    }
    state_.manager.rescan();
    const std::filesystem::path exe_dir(QCoreApplication::applicationDirPath().toStdWString());
    for (const studio::script_language& lang : studio::languages()) {
        for (const std::filesystem::path& dropped : keep_one_bridge(state_.manager, lang)) {
            callbacks_.report(QString::fromStdString(dropped_bridge_note(dropped, lang)), atp::log_level::info);
        }
        const studio::bridge_source ours = studio::find_bridge_source(state_.manager, exe_dir, lang);
        if (const std::optional<std::filesystem::path> stale =
                studio::stale_loaded_bridge(state_.manager, ours, lang)) {
            callbacks_.report(QString::fromStdString(studio::stale_bridge_note(*stale, ours.bridge)),
                              atp::log_level::warning);
        }
        if (const std::optional<std::filesystem::path> stale =
                studio::stale_loaded_package(state_.manager, ours, lang)) {
            callbacks_.report(QString::fromStdString(studio::stale_package_note(*stale, ours.package)),
                              atp::log_level::warning);
        }
    }
    state_.invalidate_descriptions();
    callbacks_.project_changed();
}

void manager_widget::refresh() {
    dirs_->clear();
    for (const auto& dir : state_.manager.search_dirs()) {
        dirs_->addItem(new QListWidgetItem(directory_icon_, QString::fromStdWString(dir.wstring())));
    }
    plugins_->clear();
    for (const plugin_info& p : state_.manager.plugins()) {
        auto* item = new QTreeWidgetItem(plugins_);
        const QString full_path = QString::fromStdWString(p.path.wstring());
        item->setText(0, QString::fromStdWString(p.path.filename().wstring()));
        item->setIcon(0, plugin_icon_);
        item->setToolTip(0, full_path);
        item->setData(0, path_role, full_path);
        if (p.loaded) {
            item->setText(1, "loaded");
            for (const module_info& m : p.modules) {
                auto* child = new QTreeWidgetItem(item);
                child->setText(0, QString::fromStdString(m.name + " " + m.ver.to_string()));
                child->setIcon(0, module_icon_);
                child->setText(1, m.broken ? "broken" : "ok");
                if (m.broken) {
                    child->setToolTip(1, QString::fromStdString(m.error));
                }
                if (!m.source.empty()) {
                    const QString source = QString::fromStdString(m.source);
                    child->setData(0, path_role, source);
                    child->setToolTip(0, source);
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

void manager_widget::show_plugin_menu(const QPoint& pos, const QPoint& global) {
    QTreeWidgetItem* item = plugins_->itemAt(pos);
    if (item == nullptr) {
        return;
    }
    const QString path = item->data(0, path_role).toString();
    if (path.isEmpty()) {
        return;
    }
    plugins_->setCurrentItem(item);

    QMenu menu;
    QAction* open = item->parent() == nullptr ? nullptr : menu.addAction(QStringLiteral("Open in editor"));
    QAction* copy = menu.addAction(QStringLiteral("Copy path"));
    QAction* chosen = menu.exec(global);
    if (chosen == copy) {
        QGuiApplication::clipboard()->setText(path);
    } else if (chosen != nullptr && chosen == open) {
        open_source(state_, callbacks_, path);
    }
}

void manager_widget::sync_settings() {
    state_.settings.search_dirs.clear();
    for (const auto& d : state_.manager.search_dirs()) {
        state_.settings.search_dirs.push_back(d.string());
    }
    state_.script_env.apply(state_.manager.search_dirs());
    save_settings(state_.settings, state_.settings_file);
}

}  // namespace atp::studio::ui
