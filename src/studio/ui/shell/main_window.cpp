// SPDX-License-Identifier: Apache-2.0
#include "shell/main_window.hpp"

#include "kit/icons.hpp"
#include "model/create_group.hpp"
#include "model/editor.hpp"
#include "model/new_script_module.hpp"
#include "shell/attach_dialog.hpp"
#include "shell/new_script_module_dialog.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

#include <QActionGroup>
#include <QApplication>
#include <QByteArray>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QGuiApplication>
#include <QMenuBar>
#include <QMessageBox>
#include <QStringList>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QTimer>
#include <QVBoxLayout>
#include <QtVersion>

#include <atp/log_pump.hpp>
#include <atp/plugin.hpp>
#include <atp/runtime/config_model.hpp>
#include <atp/studio/property_sync.hpp>
#include <atp/studio/settings.hpp>

namespace atp::studio::ui {
namespace {

constexpr int window_state_version = 2;

constexpr std::string_view site_url = "https://anitools.studio";

/// Refresh period of the local view: reading a pointer costs nothing, so it runs at the rate the eye
/// wants.
constexpr int local_poll_ms = 100;

/// Refresh period while attached. Every tick is a round trip over a socket, and a remote pipeline is
/// watched, not played — four times a second is enough to see it move and cheap enough to leave on.
constexpr int remote_poll_ms = 250;

}  // namespace

main_window::main_window(app_state& state) : state_(state), default_style_(QApplication::style()->name()) {
    resize(1600, 900);
    apply_theme();

    callbacks_.project_changed = [this] { refresh_all(); };
    callbacks_.error = [this](const QString& text) { report(text, atp::log_level::error); };
    callbacks_.report = [this](const QString& text, atp::log_level level) { report(text, level); };
    callbacks_.selection_changed = [this] {
        if (inspector_ != nullptr) {
            inspector_->refresh();
        }
    };

    log_dock_ = build_log_dock();

    tree_dock_ = new QDockWidget("Project", this);
    tree_dock_->setObjectName("dock.project");
    tree_ = new project_tree(state_, callbacks_, tree_dock_);
    tree_dock_->setWidget(tree_);

    palette_dock_ = new QDockWidget("Palette", this);
    palette_dock_->setObjectName("dock.palette");
    palette_ = new palette_widget(state_, callbacks_, palette_dock_);
    palette_dock_->setWidget(palette_);

    manager_dock_ = new QDockWidget("Plugins", this);
    manager_dock_->setObjectName("dock.plugins");
    manager_ = new manager_widget(state_, callbacks_, manager_dock_);
    manager_dock_->setWidget(manager_);

    canvas_ = new canvas_widget(state_, callbacks_, this);
    setCentralWidget(canvas_);

    inspector_dock_ = new QDockWidget("Inspector", this);
    inspector_dock_->setObjectName("dock.inspector");
    inspector_ = new inspector_widget(state_, callbacks_, inspector_dock_);
    inspector_dock_->setWidget(inspector_);

    runtime_dock_ = new QDockWidget("Runtime", this);
    runtime_dock_->setObjectName("dock.runtime");
    runtime_ = new runtime_widget(state_, callbacks_, runtime_dock_);
    runtime_dock_->setWidget(runtime_);

    apply_style();

    build_menus();

    apply_default_layout();
    restore_layout();

    poll_timer_ = new QTimer(this);
    QObject::connect(poll_timer_, &QTimer::timeout, this, [this] { poll(); });
    poll_timer_->start(local_poll_ms);

    refresh_all();
}

void main_window::apply_default_layout() {
    addDockWidget(Qt::LeftDockWidgetArea, tree_dock_);
    addDockWidget(Qt::LeftDockWidgetArea, palette_dock_);
    addDockWidget(Qt::RightDockWidgetArea, inspector_dock_);
    addDockWidget(Qt::BottomDockWidgetArea, log_dock_);
    addDockWidget(Qt::BottomDockWidgetArea, manager_dock_);
    addDockWidget(Qt::BottomDockWidgetArea, runtime_dock_);

    tabifyDockWidget(log_dock_, manager_dock_);
    log_dock_->raise();

    for (QDockWidget* dock : {tree_dock_, palette_dock_, manager_dock_, inspector_dock_, runtime_dock_, log_dock_}) {
        dock->show();
    }

    resizeDocks({tree_dock_, inspector_dock_}, {256, 315}, Qt::Horizontal);
    resizeDocks({tree_dock_, palette_dock_}, {1, 1}, Qt::Vertical);
    resizeDocks({log_dock_, runtime_dock_}, {1, 1}, Qt::Horizontal);
    resizeDocks({log_dock_}, {284}, Qt::Vertical);
}

void main_window::restore_layout() {
    if (!state_.settings.window_geometry.empty()) {
        (void)restoreGeometry(QByteArray::fromBase64(QByteArray::fromStdString(state_.settings.window_geometry)));
    }
    if (!state_.settings.window_state.empty()) {
        (void)restoreState(QByteArray::fromBase64(QByteArray::fromStdString(state_.settings.window_state)),
                           window_state_version);
    }
}

void main_window::reset_layout() {
    apply_default_layout();
}

void main_window::closeEvent(QCloseEvent* event) {
    state_.settings.window_geometry = saveGeometry().toBase64().toStdString();
    state_.settings.window_state = saveState(window_state_version).toBase64().toStdString();
    QMainWindow::closeEvent(event);
}

void main_window::refresh_all() {
    const bool locked = state_.view->running();
    const bool attached = state_.attached();
    undo_action_->setEnabled(!locked && state_.doc.can_undo());
    redo_action_->setEnabled(!locked && state_.doc.can_redo());
    new_group_action_->setEnabled(!locked);
    // Save writes to the project's own path, and a mirror has none — the path put aside belongs to
    // someone else. Save As stays on: the mirror is a valid config, so exporting it is free.
    save_action_->setEnabled(!attached);
    save_as_action_->setEnabled(true);
    attach_action_->setEnabled(!attached);
    detach_action_->setEnabled(attached);
    refresh_mirror_action_->setEnabled(attached);
    stop_remote_action_->setEnabled(attached);
    poll_timer_->setInterval(attached ? remote_poll_ms : local_poll_ms);
    recent_menu_->setEnabled(!state_.settings.recent_projects.empty());
    tree_->refresh();
    manager_->refresh();
    palette_->refresh();
    canvas_->refresh();
    inspector_->refresh();
    runtime_->refresh();
    update_title();
}

void main_window::update_title() {
    if (state_.attached()) {
        setWindowTitle(QString("attached to %1 - ATP Studio").arg(QString::fromStdString(state_.endpoint())));
        setWindowModified(false);
        return;
    }
    const QString name =
        state_.doc_path ? QString::fromStdWString(state_.doc_path->filename().wstring()) : QString("Untitled");
    setWindowTitle(name + "[*] - ATP Studio");
    setWindowModified(state_.doc.is_modified());
}

void main_window::attach_dialog_flow() {
    attach_dialog dialog(QString::fromStdString(state_.settings.attach_host), state_.settings.attach_port, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    try {
        state_.attach(dialog.host().toStdString(), static_cast<std::uint16_t>(dialog.port()));
        state_.settings.attach_host = dialog.host().toStdString();
        state_.settings.attach_port = dialog.port();
        save_settings(state_.settings, state_.settings_file);
        report(QString("attached to %1").arg(QString::fromStdString(state_.endpoint())));
    } catch (const std::exception& e) {
        report("attach", e);
    }
    refresh_all();
}

void main_window::detach(const QString& reason) {
    if (!state_.attached()) {
        return;
    }
    const QString endpoint = QString::fromStdString(state_.endpoint());
    state_.detach();
    report(reason.isEmpty() ? QString("detached from %1").arg(endpoint)
                            : QString("detached from %1: %2").arg(endpoint, reason),
           reason.isEmpty() ? atp::log_level::info : atp::log_level::warning);
    refresh_all();
}

void main_window::report(const QString& text, atp::log_level level) {
    const QString stamp = QString::fromStdString(atp::format_log_time(std::chrono::system_clock::now()));
    const QString name = QString::fromStdString(std::string(atp::level_name(level)));
    log_->append(stamp + " [" + name + "] " + text, level);
}

void main_window::report(const std::string& context, const std::exception& e) {
    report(QString::fromStdString(context + ": " + e.what()), atp::log_level::error);
}

void main_window::drain_logs() {
    for (const atp::log_line& line : state_.run.collect_logs()) {
        log_->append(QString::fromStdString(atp::format_log_line(line)), line.level);
    }
}

void main_window::open_path(const std::filesystem::path& path) {
    try {
        state_.doc = project::open(path);
        state_.doc_path = path;
        state_.reset_view();
        if (state_.doc.had_includes()) {
            report(QString("%1 uses $include — saving will write a flattened file")
                       .arg(QString::fromStdWString(path.wstring())),
                   atp::log_level::warning);
        }
        for (const std::string& p : state_.doc.config().plugins) {
            try {
                state_.manager.load_plugin(state_.config_dir() / p);
            } catch (const std::exception& e) {
                report("plugin", e);
            }
        }
        state_.invalidate_descriptions();
        note_recent(state_.settings, path);
        save_settings(state_.settings, state_.settings_file);
        refresh_all();
    } catch (const std::exception& e) {
        report("open", e);
    }
}

void main_window::build_menus() {
    QMenu* file = menuBar()->addMenu("&File");
    file->addAction(icons::new_project(), "&New", QKeySequence::New, [this] {
        state_.doc = project::create();
        state_.doc_path.reset();
        state_.reset_view();
        refresh_all();
    });
    file->addAction(icons::open_project(), "&Open...", QKeySequence::Open, [this] { open_dialog(); });
    recent_menu_ = file->addMenu(icons::recent(), "Recent Projects");
    QObject::connect(recent_menu_, &QMenu::aboutToShow, recent_menu_, [this] { rebuild_recent_menu(); });
    save_action_ = file->addAction(icons::save(), "&Save", QKeySequence::Save, [this] { save(false); });
    save_as_action_ =
        file->addAction(icons::save_as(), "Save &As...", QKeySequence("Ctrl+Shift+S"), [this] { save(true); });
    file->addSeparator();
    file->addAction(icons::module(), "&New module...", [this] { new_script_module(); });

    QMenu* edit = menuBar()->addMenu("&Edit");
    undo_action_ = edit->addAction(icons::undo(), "&Undo", QKeySequence::Undo, [this] {
        (void)state_.doc.undo();
        refresh_all();
    });
    redo_action_ = edit->addAction(icons::redo(), "&Redo", QKeySequence::Redo, [this] {
        (void)state_.doc.redo();
        refresh_all();
    });
    edit->addSeparator();
    new_group_action_ = edit->addAction(icons::new_group(), "New &group", QKeySequence("Ctrl+Shift+G"),
                                        [this] { create_group(state_, callbacks_, std::nullopt); });

    QMenu* host = menuBar()->addMenu("&Host");
    attach_action_ = host->addAction("&Attach to a running host...", [this] { attach_dialog_flow(); });
    refresh_mirror_action_ = host->addAction("&Refresh the mirror", [this] {
        try {
            state_.refresh_mirror();
            report(QString("re-read the pipeline of %1").arg(QString::fromStdString(state_.endpoint())));
        } catch (const std::exception& e) {
            report("refresh", e);
        }
        refresh_all();
    });
    detach_action_ = host->addAction("&Detach", [this] { detach(QString()); });
    host->addSeparator();
    stop_remote_action_ = host->addAction("&Stop the remote host...", [this] {
        const QString endpoint = QString::fromStdString(state_.endpoint());
        const auto answer = QMessageBox::question(
            this, "Stop the remote host",
            QString("Shut down the pipeline host at %1?\n\nIts process exits; the studio cannot start it again.")
                .arg(endpoint));
        if (answer != QMessageBox::Yes) {
            return;
        }
        try {
            state_.stop_remote();
            report(QString("asked %1 to stop").arg(endpoint));
        } catch (const std::exception& e) {
            report("stop remote", e);
        }
    });

    QMenu* view = menuBar()->addMenu("&View");
    build_view_menu(view);

    build_help_menu(menuBar()->addMenu("&Help"));
}

void main_window::build_view_menu(QMenu* view) {
    QMenu* panels = view->addMenu("&Panels");
    for (QDockWidget* dock : {tree_dock_, palette_dock_, manager_dock_, inspector_dock_, runtime_dock_, log_dock_}) {
        panels->addAction(dock->toggleViewAction());
    }

    view->addAction("Reset &Layout", [this] { reset_layout(); });
    view->addSeparator();
    build_theme_menu(view);
    build_style_menu(view);
}

void main_window::build_theme_menu(QMenu* view) {
    QMenu* theme = view->addMenu("&Theme");
    auto* group = new QActionGroup(theme);
    group->setExclusive(true);
    for (const auto& [value, label] :
         {std::pair{app_theme::system, QStringLiteral("&System")},
          std::pair{app_theme::light, QStringLiteral("&Light")}, std::pair{app_theme::dark, QStringLiteral("&Dark")}}) {
        QAction* action = theme->addAction(label);
        action->setCheckable(true);
        action->setChecked(state_.settings.theme == value);
        group->addAction(action);
        QObject::connect(action, &QAction::triggered, this, [this, value] {
            state_.settings.theme = value;
            apply_theme();
            store_settings();
        });
    }
}

void main_window::build_style_menu(QMenu* view) {
    QMenu* style = view->addMenu("St&yle");
    auto* group = new QActionGroup(style);

    const auto entry = [this, style, group](const QString& label, const std::string& value) {
        QAction* action = style->addAction(label);
        action->setCheckable(true);
        action->setChecked(state_.settings.style == value);
        group->addAction(action);
        QObject::connect(action, &QAction::triggered, this, [this, value] {
            state_.settings.style = value;
            apply_style();
            store_settings();
        });
    };

    entry(QStringLiteral("&System"), std::string());
    style->addSeparator();
    for (const QString& key : QStyleFactory::keys()) {
        entry(key, key.toStdString());
    }
}

void main_window::build_help_menu(QMenu* help) {
    help->addAction("&About ATP Studio", [this] { show_about(); });
}

void main_window::show_about() {
    QMessageBox box(this);
    box.setWindowTitle("About ATP Studio");
    box.setIconPixmap(icons::brand().pixmap(64, 64));
    box.setTextFormat(Qt::RichText);
    box.setText(QStringLiteral("<div align=\"center\"><b>ATP Studio</b> %1"
                               "<p align=\"center\">Plugin ABI %2<br>Config schema %3</p>"
                               "<p align=\"center\"><a href=\"%4\">%4</a></p>"
                               "<p align=\"center\">Copyright 2026 The AniToolsPlatform Authors<br>"
                               "Licensed under the Apache License 2.0.<br>"
                               "Built with Qt %5, used under the GNU Lesser General Public License v3.<br>"
                               "See LICENSE and THIRD-PARTY-NOTICES.md beside the program.</p></div>")
                    .arg(QString::fromLatin1(ATP_STUDIO_VERSION))
                    .arg(atp::plugin_abi)
                    .arg(QString::fromStdString(runtime::config_schema_version.to_string()),
                         QString::fromLatin1(site_url), QString::fromLatin1(qVersion())));
    box.exec();
}

void main_window::apply_theme() {
    QStyleHints* hints = QGuiApplication::styleHints();
    switch (state_.settings.theme) {
        case app_theme::light:
            hints->setColorScheme(Qt::ColorScheme::Light);
            return;
        case app_theme::dark:
            hints->setColorScheme(Qt::ColorScheme::Dark);
            return;
        case app_theme::system:
            hints->setColorScheme(Qt::ColorScheme::Unknown);
            return;
    }
}

void main_window::apply_style() {
    QString wanted = QString::fromStdString(state_.settings.style);
    if (!wanted.isEmpty() && !QStyleFactory::keys().contains(wanted)) {
        report("style '" + wanted + "' is not available here; using " + default_style_, atp::log_level::warning);
        state_.settings.style.clear();
        wanted.clear();
    }
    const QString wanted_or_default = wanted.isEmpty() ? default_style_ : wanted;
    if (QApplication::style()->name().compare(wanted_or_default, Qt::CaseInsensitive) != 0) {
        QApplication::setStyle(wanted_or_default);
        apply_theme();
    }
}

void main_window::store_settings() {
    try {
        save_settings(state_.settings, state_.settings_file);
    } catch (const std::exception& e) {
        report("settings", e);
    }
}

void main_window::rebuild_recent_menu() {
    recent_menu_->clear();
    for (const std::string& p : state_.settings.recent_projects) {
        recent_menu_->addAction(QString::fromStdString(p), [this, p] { open_path(std::filesystem::path(p)); });
    }
    recent_menu_->addSeparator();
    recent_menu_->addAction("Clear Recent", [this] {
        state_.settings.recent_projects.clear();
        store_settings();
        refresh_all();
    });
}

QDockWidget* main_window::build_log_dock() {
    auto* dock = new QDockWidget("Log", this);
    dock->setObjectName("dock.log");
    auto* body = new QWidget(dock);
    auto* layout = new QVBoxLayout(body);
    layout->setSpacing(style::row_spacing);

    log_ = new log_widget(body);
    log_->setObjectName("log.lines");
    layout->addWidget(log_, 1);

    const style::button_bar bar = style::make_button_bar(body);
    auto* clear = style::tool_button(style::glyph::clear, "clear the log", bar.box);
    QObject::connect(clear, &QToolButton::clicked, body, [this] { log_->clear(); });
    bar.row->addWidget(clear);
    bar.row->addStretch(1);
    layout->addWidget(bar.box);

    dock->setWidget(body);
    addDockWidget(Qt::BottomDockWidgetArea, dock);
    return dock;
}

void main_window::open_dialog() {
    const QString file = QFileDialog::getOpenFileName(this, "Open config", QString(), "Pipeline configs (*.json)");
    if (!file.isEmpty()) {
        open_path(std::filesystem::path(file.toStdWString()));
    }
}

void main_window::new_script_module() {
    const studio::script_language* remembered = studio::language_by_id(state_.settings.last_script_language);
    const studio::script_language& offered_language = remembered == nullptr ? studio::languages().front() : *remembered;
    const std::optional<std::filesystem::path> last =
        studio::last_script_folder(state_.manager.search_dirs(), offered_language);
    const QString offered = last ? QString::fromStdWString(last->wstring()) : QDir::homePath();
    new_script_module_dialog dialog(offered, state_.manager.registry(), offered_language.id, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const studio::script_language& chosen = dialog.language();
    state_.settings.last_script_language = std::string(chosen.id);
    const std::filesystem::path exe_dir(QCoreApplication::applicationDirPath().toStdWString());
    const std::optional<std::filesystem::path> file = create_script_module_action(
        state_, callbacks_, chosen, std::filesystem::path(dialog.directory().toStdWString()),
        dialog.module_name().toStdString(), studio::find_bridge_source(state_.manager, exe_dir, chosen));
    if (!file) {
        return;
    }
    if (!open_in_editor(QString::fromStdString(state_.settings.editor_command), *file)) {
        report(
            QString("could not open an editor for %1; open it by hand").arg(QString::fromStdWString(file->wstring())),
            atp::log_level::warning);
    }
}

void main_window::save(bool ask_path) {
    std::filesystem::path target;
    if (!ask_path && state_.doc_path) {
        target = *state_.doc_path;
    } else {
        const QString file = QFileDialog::getSaveFileName(this, "Save config", QString(), "Pipeline configs (*.json)");
        if (file.isEmpty()) {
            return;
        }
        target = std::filesystem::path(file.toStdWString());
    }
    try {
        if (state_.run.running()) {
            if (atp::group* root = state_.run.live_root()) {
                sync_persistent_properties(state_.doc, state_.doc.config(), *root);
            }
        }
        state_.doc.save(target);
        if (state_.attached()) {
            report(QString("saved a mirror of %1: it carries the graph, not the plugins, threads "
                           "or assignments the host was started with")
                       .arg(QString::fromStdString(state_.endpoint())),
                   atp::log_level::warning);
            refresh_all();
            return;
        }
        state_.doc_path = target;
        note_recent(state_.settings, target);
        save_settings(state_.settings, state_.settings_file);
        refresh_all();
    } catch (const std::exception& e) {
        report("save", e);
    }
}

void main_window::poll() {
    if (state_.attached()) {
        if (!state_.view->running()) {
            const std::string reason = state_.view->error_text();
            detach(QString::fromStdString(reason.empty() ? "the host is gone" : reason));
            return;
        }
        runtime_->refresh();
        canvas_->scene().update_samples();
        return;
    }
    drain_logs();
    if (const std::string error = state_.view->error_text(); !error.empty()) {
        report(QString::fromStdString("pipeline: " + error), atp::log_level::error);
        state_.run.stop();
        refresh_all();
        return;
    }
    if (state_.view->running()) {
        runtime_->refresh();
        canvas_->scene().update_samples();
    }
}

}  // namespace atp::studio::ui
