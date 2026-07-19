#ifndef ATP_STUDIO_QT_MAIN_WINDOW_HPP
#define ATP_STUDIO_QT_MAIN_WINDOW_HPP

#include <exception>
#include <filesystem>
#include <string>

#include <QAction>
#include <QDockWidget>
#include <QFileDialog>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QMenuBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <atp/studio/qt/app_state.hpp>

namespace atp::studio::qt {

// Главное окно: меню + доки. Вся логика — в ядре (document/session/manager),
// окно только маршрутизирует действия и перестраивает виджеты.
class main_window final : public QMainWindow {
   public:
    explicit main_window(app_state& state) : state_(state) {
        setWindowTitle("Studio");
        resize(1600, 900);

        callbacks_.document_changed = [this] { refresh_all(); };
        callbacks_.error = [this](const QString& text) { report(text); };
        callbacks_.selection_changed = [this] { /* инспектор придёт задачей 4 */ };

        build_menus();
        build_errors_dock();
        setCentralWidget(new QLabel("canvas here", this));  // заменит задача 3

        // 10 Гц: опрос ошибок исполнения и статистики (наполняется задачей 5)
        auto* timer = new QTimer(this);
        QObject::connect(timer, &QTimer::timeout, this, [this] { poll(); });
        timer->start(100);

        refresh_all();
    }

    void refresh_all() {
        const bool locked = state_.run.running();
        undo_action_->setEnabled(!locked && state_.doc.can_undo());
        redo_action_->setEnabled(!locked && state_.doc.can_redo());
        save_action_->setEnabled(!locked);
        save_as_action_->setEnabled(!locked);
        // задачи 2-5 дополняют: палитра, менеджер, канвас, инспектор, runtime
    }

    void report(const QString& text) {
        errors_->insertItem(0, text);  // свежее сверху
    }

    void report(const std::string& context, const std::exception& e) {
        report(QString::fromStdString(context + ": " + e.what()));
    }

   private:
    void build_menus() {
        QMenu* file = menuBar()->addMenu("&File");
        file->addAction("&New", [this] {
            state_.doc = document::create();
            state_.doc_path.reset();
            state_.reset_view();
            refresh_all();
        });
        file->addAction("&Open...", [this] { open_dialog(); });
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

    void build_errors_dock() {
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

    void open_dialog() {
        const QString file = QFileDialog::getOpenFileName(this, "Open config", QString(), "Pipeline configs (*.json)");
        if (file.isEmpty()) {
            return;
        }
        try {
            state_.doc = document::open(std::filesystem::path(file.toStdWString()));
            state_.doc_path = std::filesystem::path(file.toStdWString());
            state_.reset_view();
            if (state_.doc.had_includes()) {
                report(QString("warning: %1 uses $include — saving will write a flattened file").arg(file));
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
            refresh_all();
        } catch (const std::exception& e) {
            report("open", e);
        }
    }

    void save(bool ask_path) {
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
        } catch (const std::exception& e) {
            report("save", e);
        }
    }

    void poll() {
        // наполняется задачей 5 (ошибки исполнения, статистика, мониторинг)
    }

    app_state& state_;
    ui_callbacks callbacks_;
    QListWidget* errors_ = nullptr;
    QAction* save_action_ = nullptr;
    QAction* save_as_action_ = nullptr;
    QAction* undo_action_ = nullptr;
    QAction* redo_action_ = nullptr;
};

}  // namespace atp::studio::qt

#endif  // ATP_STUDIO_QT_MAIN_WINDOW_HPP
