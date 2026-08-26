// SPDX-License-Identifier: Apache-2.0
#include "canvas/canvas_widget.hpp"

#include "model/create_group.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>

#include <QEvent>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPoint>
#include <QPushButton>
#include <QRectF>
#include <QScrollBar>
#include <QTransform>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace atp::studio::ui {
namespace {

/// Range the view may be scaled to, and the ratio one step of the wheel or the menu moves it by.
/// Three octaves either side of life size is as far as a pipeline is worth looking at from, and a
/// ratio rather than an increment is what makes zooming out undo zooming in exactly.
constexpr double zoom_min = 0.125;
constexpr double zoom_max = 8.0;
constexpr double zoom_step = 1.25;

/// Margin left around the group when the view is fitted to it, so the outermost stub labels do not
/// end up against the frame.
constexpr double fit_margin = 24.0;

}  // namespace

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
    view_->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    view_->viewport()->installEventFilter(this);
    view_->viewport()->setMouseTracking(true);
    scene_->on_fit_to_window = [this] { zoom_to_fit(); };
    scene_->on_actual_size = [this] { zoom_reset(); };
    apply_colors();
}

double canvas_widget::zoom() const {
    return zoom_;
}

void canvas_widget::apply_zoom() {
    zoom_ = std::clamp(zoom_, zoom_min, zoom_max);
    view_->setTransform(QTransform::fromScale(zoom_, zoom_));
}

void canvas_widget::zoom_in() {
    zoom_ *= zoom_step;
    apply_zoom();
}

void canvas_widget::zoom_out() {
    zoom_ /= zoom_step;
    apply_zoom();
}

void canvas_widget::zoom_reset() {
    zoom_ = 1.0;
    apply_zoom();
}

void canvas_widget::zoom_to_fit() {
    const QRectF content = scene_->itemsBoundingRect();
    if (content.isEmpty()) {
        return;
    }
    view_->fitInView(content.adjusted(-fit_margin, -fit_margin, fit_margin, fit_margin), Qt::KeepAspectRatio);
    zoom_ = std::clamp(view_->transform().m11(), zoom_min, zoom_max);
    const QGraphicsView::ViewportAnchor anchor = view_->transformationAnchor();
    view_->setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    view_->setTransform(QTransform::fromScale(zoom_, zoom_));
    view_->setTransformationAnchor(anchor);
    view_->centerOn(content.center());
}

bool canvas_widget::eventFilter(QObject* watched, QEvent* event) {
    if (watched != view_->viewport()) {
        return QWidget::eventFilter(watched, event);
    }
    switch (event->type()) {
        case QEvent::Wheel: {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
            auto* wheel = static_cast<QWheelEvent*>(event);
            const int notch = wheel->angleDelta().y();
            if (notch == 0 || (wheel->modifiers() & Qt::ControlModifier) == 0) {
                return QWidget::eventFilter(watched, event);
            }
            if (notch > 0) {
                zoom_in();
            } else {
                zoom_out();
            }
            return true;
        }
        case QEvent::MouseButtonPress: {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
            auto* press = static_cast<QMouseEvent*>(event);
            if (press->button() != Qt::MiddleButton) {
                break;
            }
            panning_ = true;
            pan_from_ = press->position().toPoint();
            view_->viewport()->setCursor(Qt::ClosedHandCursor);
            return true;
        }
        case QEvent::MouseMove: {
            if (!panning_) {
                break;
            }
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
            const QPoint at = static_cast<QMouseEvent*>(event)->position().toPoint();
            const QPoint by = at - pan_from_;
            pan_from_ = at;
            view_->horizontalScrollBar()->setValue(view_->horizontalScrollBar()->value() - by.x());
            view_->verticalScrollBar()->setValue(view_->verticalScrollBar()->value() - by.y());
            return true;
        }
        case QEvent::MouseButtonRelease: {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
            if (!panning_ || static_cast<QMouseEvent*>(event)->button() != Qt::MiddleButton) {
                break;
            }
            panning_ = false;
            view_->viewport()->unsetCursor();
            return true;
        }
        default:
            break;
    }
    return QWidget::eventFilter(watched, event);
}

void canvas_widget::refresh() {
    apply_colors();
    rebuild_crumbs();
    scene_->rebuild();
}

void canvas_widget::apply_colors() {
    scene_->set_colors(canvas_colors(palette()));
}

void canvas_widget::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    const QEvent::Type type = event->type();
    if (type == QEvent::PaletteChange || type == QEvent::ApplicationPaletteChange || type == QEvent::StyleChange) {
        apply_colors();
        scene_->rebuild();
    }
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
    auto add_separator = [this] {
        auto* mark = new QLabel(QStringLiteral("/"), crumbs_);
        style::muted(mark);
        crumbs_->layout()->addWidget(mark);
    };
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
        add_separator();
        add_crumb(QString::fromStdString(segment), walked);
        if (dot == std::string::npos) {
            break;
        }
        begin = dot + 1;
    }
    auto* row = qobject_cast<QHBoxLayout*>(crumbs_->layout());
    row->addStretch(1);
    auto* add = new QPushButton(QString(QChar(style::glyph::add)) + " group", crumbs_);
    add->setFlat(true);
    add->setToolTip("add an empty group to the group shown");
    add->setEnabled(!state_.view->running());
    QObject::connect(add, &QPushButton::clicked, this, [this] { create_group(state_, callbacks_, std::nullopt); });
    row->addWidget(add);
}

}  // namespace atp::studio::ui
