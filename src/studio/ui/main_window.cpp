#include "main_window.hpp"

#include <QDockWidget>
#include <QFileDialog>
#include <QMenuBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

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

    // 10 Гц: опрос ошибок исполнения и статистики (наполняется задачей 5)
    auto* timer = new QTimer(this);
    QObject::connect(timer, &QTimer::timeout, this, [this] { poll(); });
    timer->start(100);

    refresh_all();
}

void main_window::refresh_all() {
    const bool locked = state_.run.running();
    undo_action_->setEnabled(!locked && state_.doc.can_undo());
    redo_action_->setEnabled(!locked && state_.doc.can_redo());
    save_action_->setEnabled(!locked);
    save_as_action_->setEnabled(!locked);
    recent_menu_->setEnabled(!state_.settings.recent_projects.empty());
    manager_->refresh();
    palette_->refresh();
    canvas_->refresh();
    inspector_->refresh();
    runtime_->refresh();
}

void main_window::report(const QString& text) {
    errors_->insertItem(0, text);  // свежее сверху
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
        // плагины конфига — в реестр сессии: палитре и канвасу нужны порты
        for (const std::string& p : state_.doc.config().plugins) {
            try {
                state_.manager.load_plugin(state_.config_dir() / p);
            } catch (const std::exception& e) {
                report("plugin", e);
            }
        }
        state_.describe_cache.clear();
        note_recent(state_.settings, path);
        save_settings(state_.settings, state_.settings_file);  // сразу: крэш не должен терять список
        refresh_all();
    } catch (const std::exception& e) {
        report("open", e);
    }
}

void main_window::build_menus() {
    QMenu* file = menuBar()->addMenu("&File");
    file->addAction("&New", [this] {
        state_.doc = document::create();
        state_.doc_path.reset();
        state_.reset_view();
        refresh_all();
    });
    file->addAction("&Open...", [this] { open_dialog(); });
    recent_menu_ = file->addMenu("Recent Projects");
    // Лениво на aboutToShow, а не в refresh_all: клик по пункту ведёт в
    // open_path → refresh_all, и clear() там удалил бы QAction, чей
    // triggered ещё на стеке — у Qt удаление отправителя из слота
    // небезопасно. aboutToShow перестраивает вне эмиссии.
    QObject::connect(recent_menu_, &QMenu::aboutToShow, recent_menu_, [this] { rebuild_recent_menu(); });
    save_action_ = file->addAction("&Save", [this] { save(false); });
    save_as_action_ = file->addAction("Save &As...", [this] { save(true); });

    QMenu* edit = menuBar()->addMenu("&Edit");
    undo_action_ = edit->addAction("&Undo", QKeySequence::Undo, [this] {
        (void)state_.doc.undo();
        refresh_all();
    });
    redo_action_ = edit->addAction("&Redo", QKeySequence::Redo, [this] {
        (void)state_.doc.redo();
        refresh_all();
    });
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
    auto* clear = new QPushButton("Clear", body);
    errors_ = new QListWidget(body);
    QObject::connect(clear, &QPushButton::clicked, body, [this] { errors_->clear(); });
    layout->addWidget(clear);
    layout->addWidget(errors_);
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
        const QString file =
            QFileDialog::getSaveFileName(this, "Save config", QString(), "Pipeline configs (*.json)");
        if (file.isEmpty()) {
            return;
        }
        target = std::filesystem::path(file.toStdWString());
    }
    try {
        state_.doc.save(target);
        state_.doc_path = target;
        note_recent(state_.settings, target);
        save_settings(state_.settings, state_.settings_file);
    } catch (const std::exception& e) {
        report("save", e);
    }
}

void main_window::poll() {
    // авария исполнения: первопричина — в журнал, пайплайн гасится
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
