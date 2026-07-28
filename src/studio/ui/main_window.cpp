#include "main_window.hpp"

#include "create_group.hpp"
#include "icons.hpp"

#include <optional>

#include <QDockWidget>
#include <QFileDialog>
#include <QMenuBar>
#include <QTimer>
#include <QVBoxLayout>

#include <atp/studio/property_sync.hpp>
#include <atp/studio/settings.hpp>

namespace atp::studio::ui {

main_window::main_window(app_state& state) : state_(state) {
    setWindowTitle("Studio");
    resize(1600, 900);

    callbacks_.document_changed = [this] { refresh_all(); };
    callbacks_.error = [this](const QString& text) { report(text); };
    callbacks_.selection_changed = [this] {
        if (inspector_ != nullptr) {
            inspector_->refresh();
        }
    };

    build_menus();
    build_errors_dock();

    auto* manager_dock = new QDockWidget("Modules", this);
    manager_ = new manager_widget(state_, callbacks_, manager_dock);
    manager_dock->setWidget(manager_);
    addDockWidget(Qt::LeftDockWidgetArea, manager_dock);

    auto* palette_dock = new QDockWidget("Palette", this);
    palette_ = new palette_widget(state_, callbacks_, palette_dock);
    palette_dock->setWidget(palette_);
    addDockWidget(Qt::LeftDockWidgetArea, palette_dock);

    canvas_ = new canvas_widget(state_, callbacks_, this);
    setCentralWidget(canvas_);

    auto* inspector_dock = new QDockWidget("Inspector", this);
    inspector_ = new inspector_widget(state_, callbacks_, inspector_dock);
    inspector_dock->setWidget(inspector_);
    addDockWidget(Qt::RightDockWidgetArea, inspector_dock);

    auto* runtime_dock = new QDockWidget("Runtime", this);
    runtime_ = new runtime_widget(state_, callbacks_, runtime_dock);
    runtime_dock->setWidget(runtime_);
    addDockWidget(Qt::BottomDockWidgetArea, runtime_dock);

    // 10 Hz: polling for execution errors and statistics.
    auto* timer = new QTimer(this);
    QObject::connect(timer, &QTimer::timeout, this, [this] { poll(); });
    timer->start(100);

    refresh_all();
}

void main_window::refresh_all() {
    const bool locked = state_.run.running();
    undo_action_->setEnabled(!locked && state_.doc.can_undo());
    redo_action_->setEnabled(!locked && state_.doc.can_redo());
    new_group_action_->setEnabled(!locked);
    // Saving is not locked by a running pipeline: properties are edited on the fly, and the result
    // has to be savable right there — save pulls them out of the live modules.
    save_action_->setEnabled(true);
    save_as_action_->setEnabled(true);
    recent_menu_->setEnabled(!state_.settings.recent_projects.empty());
    manager_->refresh();
    palette_->refresh();
    canvas_->refresh();
    inspector_->refresh();
    runtime_->refresh();
}

void main_window::report(const QString& text) {
    errors_->insertItem(0, text);  // newest on top
}

void main_window::report(const std::string& context, const std::exception& e) {
    report(QString::fromStdString(context + ": " + e.what()));
}

void main_window::open_path(const std::filesystem::path& path) {
    try {
        state_.doc = document::open(path);
        state_.doc_path = path;
        state_.reset_view();
        if (state_.doc.had_includes()) {
            report(QString("warning: %1 uses $include — saving will write a flattened file")
                       .arg(QString::fromStdWString(path.wstring())));
        }
        // Config plugins go into the session registry: the palette and the canvas need the ports.
        for (const std::string& p : state_.doc.config().plugins) {
            try {
                state_.manager.load_plugin(state_.config_dir() / p);
            } catch (const std::exception& e) {
                report("plugin", e);
            }
        }
        state_.invalidate_descriptions();
        note_recent(state_.settings, path);
        save_settings(state_.settings, state_.settings_file);  // at once: a crash must not lose the list
        refresh_all();
    } catch (const std::exception& e) {
        report("open", e);
    }
}

void main_window::build_menus() {
    QMenu* file = menuBar()->addMenu("&File");
    file->addAction(icons::new_document(), "&New", QKeySequence::New, [this] {
        state_.doc = document::create();
        state_.doc_path.reset();
        state_.reset_view();
        refresh_all();
    });
    file->addAction(icons::open_document(), "&Open...", QKeySequence::Open, [this] { open_dialog(); });
    recent_menu_ = file->addMenu(icons::recent(), "Recent Projects");
    // Rebuilt lazily on aboutToShow rather than in refresh_all: clicking an item leads to
    // open_path → refresh_all, where clear() would delete the QAction whose triggered signal is
    // still on the stack — deleting the sender from a slot is unsafe in Qt. aboutToShow rebuilds
    // outside the emission.
    QObject::connect(recent_menu_, &QMenu::aboutToShow, recent_menu_, [this] { rebuild_recent_menu(); });
    save_action_ = file->addAction(icons::save(), "&Save", QKeySequence::Save, [this] { save(false); });
    // Spelled out rather than taken from QKeySequence: the standard keys have no entry for "save
    // as", and Ctrl+Shift+S is what every editor on this platform binds it to anyway.
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
    // The canvas gesture needs a place on the menu bar too: a right click on empty space is not
    // something a new user finds on their own.
    new_group_action_ = edit->addAction(icons::new_group(), "New &group", QKeySequence("Ctrl+Shift+G"),
                                        [this] { create_group(state_, callbacks_, std::nullopt); });
}

void main_window::rebuild_recent_menu() {
    recent_menu_->clear();
    for (const std::string& p : state_.settings.recent_projects) {
        recent_menu_->addAction(QString::fromStdString(p), [this, p] { open_path(std::filesystem::path(p)); });
    }
    recent_menu_->addSeparator();
    recent_menu_->addAction("Clear Recent", [this] {
        state_.settings.recent_projects.clear();
        save_settings(state_.settings, state_.settings_file);
        refresh_all();
    });
}

void main_window::build_errors_dock() {
    auto* dock = new QDockWidget("Errors", this);
    auto* body = new QWidget(dock);
    auto* layout = new QVBoxLayout(body);
    layout->setSpacing(style::row_spacing);
    layout->addWidget(style::section_header("log", body));

    errors_ = new QListWidget(body);
    style::embed_view(errors_);
    layout->addWidget(errors_, 1);

    const style::button_bar bar = style::make_button_bar(body);
    auto* clear = style::tool_button(style::glyph::clear, "clear the log", bar.box);
    QObject::connect(clear, &QToolButton::clicked, body, [this] { errors_->clear(); });
    bar.row->addWidget(clear);
    bar.row->addStretch(1);
    layout->addWidget(bar.box);

    dock->setWidget(body);
    addDockWidget(Qt::BottomDockWidgetArea, dock);
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
        // While running, pull the persistent properties out of the live modules first, so that
        // on-the-fly edits and a module's own writes reach the file.
        if (state_.run.running()) {
            if (atp::group* root = state_.run.live_root()) {
                sync_persistent_properties(state_.doc, state_.doc.config(), *root);
            }
        }
        state_.doc.save(target);
        state_.doc_path = target;
        note_recent(state_.settings, target);
        save_settings(state_.settings, state_.settings_file);
    } catch (const std::exception& e) {
        report("save", e);
    }
}

void main_window::poll() {
    // Execution failure: the root cause goes to the log and the pipeline is shut down.
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
