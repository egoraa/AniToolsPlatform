#ifndef ATP_STUDIO_QT_CANVAS_WIDGET_HPP
#define ATP_STUDIO_QT_CANVAS_WIDGET_HPP

#include <cstddef>
#include <string>

#include <QGraphicsView>
#include <QHBoxLayout>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include <atp/studio/qt/canvas_scene.hpp>

namespace atp::studio::qt {

class canvas_widget final : public QWidget {
   public:
    canvas_widget(app_state& state, ui_callbacks& callbacks, QWidget* parent = nullptr)
        : QWidget(parent), state_(state), callbacks_(callbacks) {
        auto* layout = new QVBoxLayout(this);
        crumbs_ = new QWidget(this);
        crumbs_->setLayout(new QHBoxLayout(crumbs_));
        crumbs_->layout()->setContentsMargins(0, 0, 0, 0);
        scene_ = new canvas_scene(state, callbacks, this);
        view_ = new QGraphicsView(scene_, this);
        view_->setRenderHint(QPainter::Antialiasing);
        view_->setDragMode(QGraphicsView::RubberBandDrag);
        // Сброс принимает вьюпорт, а не сам view: setupViewport копирует флаг в
        // момент создания вьюпорта, то есть до этой строки — поэтому явно оба.
        // Дальше QGraphicsView сам транслирует событие сцене.
        view_->setAcceptDrops(true);
        view_->viewport()->setAcceptDrops(true);
        layout->addWidget(crumbs_);
        layout->addWidget(view_, 1);
    }

    void refresh() {
        rebuild_crumbs();
        scene_->rebuild();
    }

    [[nodiscard]] canvas_scene& scene() {
        return *scene_;
    }

   private:
    void rebuild_crumbs() {
        // пересборка кнопок-сегментов пути: root / sub / subsub
        QLayoutItem* old = nullptr;
        while ((old = crumbs_->layout()->takeAt(0)) != nullptr) {
            delete old->widget();
            delete old;
        }
        auto add_crumb = [this](const QString& text, const std::string& path) {
            auto* button = new QPushButton(text, crumbs_);
            button->setFlat(true);
            QObject::connect(button, &QPushButton::clicked, this, [this, path] {
                state_.current_group = path;
                state_.selected_child.clear();
                callbacks_.document_changed();
            });
            crumbs_->layout()->addWidget(button);
        };
        add_crumb("root", "");
        std::string walked;
        for (std::size_t begin = 0; begin < state_.current_group.size();) {
            const std::size_t dot = state_.current_group.find('.', begin);
            const std::string segment =
                state_.current_group.substr(begin, dot == std::string::npos ? dot : dot - begin);
            walked = walked.empty() ? segment : walked + "." + segment;
            add_crumb(QString::fromStdString("/ " + segment), walked);
            if (dot == std::string::npos) {
                break;
            }
            begin = dot + 1;
        }
        static_cast<QHBoxLayout*>(crumbs_->layout())->addStretch(1);
    }

    app_state& state_;
    ui_callbacks& callbacks_;
    QWidget* crumbs_ = nullptr;
    canvas_scene* scene_ = nullptr;
    QGraphicsView* view_ = nullptr;
};

}  // namespace atp::studio::qt

#endif  // ATP_STUDIO_QT_CANVAS_WIDGET_HPP
