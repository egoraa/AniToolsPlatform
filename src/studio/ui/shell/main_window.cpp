#include "shell/main_window.hpp"

#include "kit/icons.hpp"
#include "model/create_group.hpp"

#include <optional>
#include <utility>

#include <QActionGroup>
#include <QApplication>
#include <QByteArray>
#include <QDesktopServices>
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
#include <QUrl>
#include <QVBoxLayout>

#include <atp/plugin.hpp>
#include <atp/runtime/config_model.hpp>
#include <atp/studio/property_sync.hpp>
#include <atp/studio/settings.hpp>

namespace atp::studio::ui {
namespace {

constexpr int window_state_version = 2;

constexpr char site_url[] = "https://anitools.studio";

}  // namespace

main_window::main_window(app_state& state) : state_(state), default_style_(QApplication::style()->name()) {
    resize(1600, 900);
    apply_theme();

    callbacks_.project_changed = [this] { refresh_all(); };
    callbacks_.error = [this](const QString& text) { report(text); };
    callbacks_.selection_changed = [this] {
        if (inspector_ != nullptr) {
            inspector_->refresh();
        }
    };

    errors_dock_ = build_errors_dock();

    tree_dock_ = new QDockWidget("Project", this);
    tree_dock_->setObjectName("dock.project");
    tree_ = new project_tree(state_, callbacks_, tree_dock_);
    tree_dock_->setWidget(tree_);

    palette_dock_ = new QDockWidget("Palette", this);
    palette_dock_->setObjectName("dock.palette");
    palette_ = new palette_widget(state_, callbacks_, palette_dock_);
    palette_dock_->setWidget(palette_);

    manager_dock_ = new QDockWidget("Modules", this);
    manager_dock_->setObjectName("dock.modules");
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

    auto* timer = new QTimer(this);
    QObject::connect(timer, &QTimer::timeout, this, [this] { poll(); });
    timer->start(100);

    refresh_all();
}

void main_window::apply_default_layout() {
    addDockWidget(Qt::LeftDockWidgetArea, tree_dock_);
    addDockWidget(Qt::LeftDockWidgetArea, palette_dock_);
    addDockWidget(Qt::RightDockWidgetArea, inspector_dock_);
    addDockWidget(Qt::BottomDockWidgetArea, errors_dock_);
    addDockWidget(Qt::BottomDockWidgetArea, manager_dock_);
    addDockWidget(Qt::BottomDockWidgetArea, runtime_dock_);

    tabifyDockWidget(errors_dock_, manager_dock_);
    errors_dock_->raise();

    for (QDockWidget* dock : {tree_dock_, palette_dock_, manager_dock_, inspector_dock_, runtime_dock_, errors_dock_}) {
        dock->show();
    }

    resizeDocks({tree_dock_, inspector_dock_}, {256, 315}, Qt::Horizontal);
    resizeDocks({tree_dock_, palette_dock_}, {1, 1}, Qt::Vertical);
    resizeDocks({errors_dock_, runtime_dock_}, {1, 1}, Qt::Horizontal);
    resizeDocks({errors_dock_}, {284}, Qt::Vertical);
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
    const bool locked = state_.run.running();
    undo_action_->setEnabled(!locked && state_.doc.can_undo());
    redo_action_->setEnabled(!locked && state_.doc.can_redo());
    new_group_action_->setEnabled(!locked);
    save_action_->setEnabled(true);
    save_as_action_->setEnabled(true);
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
    const QString name =
        state_.doc_path ? QString::fromStdWString(state_.doc_path->filename().wstring()) : QString("Untitled");
    setWindowTitle(name + "[*] - ATP Studio");
    setWindowModified(state_.doc.is_modified());
}

void main_window::report(const QString& text) {
    errors_->insertItem(0, text);
}

void main_window::report(const std::string& context, const std::exception& e) {
    report(QString::fromStdString(context + ": " + e.what()));
}

void main_window::open_path(const std::filesystem::path& path) {
    try {
        state_.doc = project::open(path);
        state_.doc_path = path;
        state_.reset_view();
        if (state_.doc.had_includes()) {
            report(QString("warning: %1 uses $include — saving will write a flattened file")
                       .arg(QString::fromStdWString(path.wstring())));
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

    QMenu* view = menuBar()->addMenu("&View");
    build_view_menu(view);

    build_help_menu(menuBar()->addMenu("&Help"));
}

void main_window::build_view_menu(QMenu* view) {
    QMenu* panels = view->addMenu("&Panels");
    for (QDockWidget* dock : {tree_dock_, palette_dock_, manager_dock_, inspector_dock_, runtime_dock_, errors_dock_}) {
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
    const std::pair<app_theme, QString> entries[] = {{app_theme::system, QStringLiteral("&System")},
                                                     {app_theme::light, QStringLiteral("&Light")},
                                                     {app_theme::dark, QStringLiteral("&Dark")}};
    for (const auto& [value, label] : entries) {
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
    help->addSeparator();
    help->addAction("anitools.&studio", [] { (void)QDesktopServices::openUrl(QUrl(QString::fromLatin1(site_url))); });
}

void main_window::show_about() {
    QMessageBox box(this);
    box.setWindowTitle("About ATP Studio");
    box.setIconPixmap(icons::brand().pixmap(64, 64));
    box.setTextFormat(Qt::RichText);
    box.setText(
        QStringLiteral("<b>ATP Studio</b> %1"
                       "<p>Plugin ABI %2<br>Config schema %3</p>"
                       "<p><a href=\"%4\">%4</a></p>")
            .arg(QString::fromLatin1(ATP_STUDIO_VERSION))
            .arg(atp::plugin_abi)
            .arg(QString::fromStdString(runtime::config_schema_version.to_string()), QString::fromLatin1(site_url)));
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
        report("style '" + wanted + "' is not available here; using " + default_style_);
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

QDockWidget* main_window::build_errors_dock() {
    auto* dock = new QDockWidget("Errors", this);
    dock->setObjectName("dock.errors");
    auto* body = new QWidget(dock);
    auto* layout = new QVBoxLayout(body);
    layout->setSpacing(style::row_spacing);
    layout->addWidget(style::section_header("log", body));

    errors_ = new log_widget(body);
    layout->addWidget(errors_, 1);

    const style::button_bar bar = style::make_button_bar(body);
    auto* clear = style::tool_button(style::glyph::clear, "clear the log", bar.box);
    QObject::connect(clear, &QToolButton::clicked, body, [this] { errors_->clear(); });
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
        state_.doc_path = target;
        note_recent(state_.settings, target);
        save_settings(state_.settings, state_.settings_file);
        refresh_all();
    } catch (const std::exception& e) {
        report("save", e);
    }
}

void main_window::poll() {
    if (std::exception_ptr error = state_.run.error()) {
        try {
            std::rethrow_exception(error);
        } catch (const std::exception& e) {
            report("pipeline", e);
        } catch (...) {
            report(QString("pipeline: unknown error"));
        }
        state_.run.stop();
        refresh_all();
        return;
    }
    if (state_.run.running()) {
        runtime_->refresh();
        canvas_->scene().update_samples();
    }
}

}  // namespace atp::studio::ui
