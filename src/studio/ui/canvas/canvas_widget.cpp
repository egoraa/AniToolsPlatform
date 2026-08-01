#include "canvas/canvas_widget.hpp"

#include "model/create_group.hpp"

#include <cstddef>
#include <optional>
#include <string>

#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace atp::studio::ui {

canvas_widget::canvas_widget(app_state& state, ui_callbacks& callbacks, QWidget* parent)
    : QWidget(parent), state_(state), callbacks_(callbacks) {
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(style::row_spacing);
    crumbs_ = new QWidget(this);
    crumbs_->setLayout(new QHBoxLayout(crumbs_));
    crumbs_->layout()->setContentsMargins(0, 0, 0, 0);
    auto* rule = new QFrame(this);
    rule->setFrameShape(QFrame::HLine);
    rule->setFrameShadow(QFrame::Sunken);
    scene_ = new canvas_scene(state, callbacks, this);
    view_ = new QGraphicsView(scene_, this);
    view_->setRenderHint(QPainter::Antialiasing);
    view_->setDragMode(QGraphicsView::RubberBandDrag);
    view_->setAcceptDrops(true);
    view_->viewport()->setAcceptDrops(true);
    layout->addWidget(crumbs_);
    layout->addWidget(rule);
    layout->addWidget(view_, 1);
}

void canvas_widget::refresh() {
    rebuild_crumbs();
    scene_->rebuild();
}

canvas_scene& canvas_widget::scene() {
    return *scene_;
}

void canvas_widget::rebuild_crumbs() {
    QLayoutItem* old = nullptr;
    while ((old = crumbs_->layout()->takeAt(0)) != nullptr) {
        if (QWidget* w = old->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete old;
    }
    auto add_crumb = [this](const QString& text, const std::string& path) {
        auto* button = new QPushButton(text, crumbs_);
        button->setFlat(true);
        if (path == state_.current_group) {
            QFont f = button->font();
            f.setBold(true);
            button->setFont(f);
        }
        QObject::connect(button, &QPushButton::clicked, this, [this, path] {
            state_.current_group = path;
            state_.selected_child.clear();
            callbacks_.project_changed();
        });
        crumbs_->layout()->addWidget(button);
    };
    add_crumb("root", "");
    std::string walked;
    for (std::size_t begin = 0; begin < state_.current_group.size();) {
        const std::size_t dot = state_.current_group.find('.', begin);
        const std::string segment = state_.current_group.substr(begin, dot == std::string::npos ? dot : dot - begin);
        walked = walked.empty() ? segment : walked + "." + segment;
        add_crumb(QString::fromStdString("/ " + segment), walked);
        if (dot == std::string::npos) {
            break;
        }
        begin = dot + 1;
    }
    auto* row = static_cast<QHBoxLayout*>(crumbs_->layout());
    row->addStretch(1);
    auto* add = new QPushButton(QString(QChar(style::glyph::add)) + " group", crumbs_);
    add->setFlat(true);
    add->setToolTip("add an empty group to the group shown");
    add->setEnabled(!state_.run.running());
    QObject::connect(add, &QPushButton::clicked, this, [this] { create_group(state_, callbacks_, std::nullopt); });
    row->addWidget(add);
}

}  // namespace atp::studio::ui
