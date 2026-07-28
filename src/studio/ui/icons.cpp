#include "icons.hpp"

#include <algorithm>

#include <QGuiApplication>
#include <QIconEngine>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>

namespace atp::studio::ui::icons {
namespace {

/// The square the drawings are laid out on. Wide enough for half-unit detail, small enough to keep
/// the coordinates in the drawings readable.
constexpr qreal grid = 24.0;

/// Line weight, in grid units. At a 16 px menu icon this lands just above one device pixel — thin
/// enough to read as line art, thick enough to survive the scaling.
constexpr qreal weight = 1.8;

/// How much quieter an icon is than the text it labels: at full strength a row of icons pulls the
/// eye away from the words, which are what the user is actually reading.
constexpr float icon_alpha = 0.8f;

/// A drawing: it gets a painter already scaled to the grid and carrying the pen to draw with.
using draw_fn = void (*)(QPainter&);

// --- the drawings --------------------------------------------------------------------------------

/// A sheet of paper with its top right corner folded.
void sheet(QPainter& p) {
    QPainterPath path;
    path.moveTo(6.5, 2.5);
    path.lineTo(14.0, 2.5);
    path.lineTo(18.5, 7.0);
    path.lineTo(18.5, 21.5);
    path.lineTo(6.5, 21.5);
    path.closeSubpath();
    p.drawPath(path);
    p.drawPolyline(QPolygonF({{14.0, 2.5}, {14.0, 7.0}, {18.5, 7.0}}));
}

/// A folder, seen from the front.
void folder(QPainter& p) {
    QPainterPath path;
    path.moveTo(2.5, 19.5);
    path.lineTo(2.5, 5.5);
    path.lineTo(9.0, 5.5);
    path.lineTo(11.0, 8.0);
    path.lineTo(21.5, 8.0);
    path.lineTo(21.5, 19.5);
    path.closeSubpath();
    p.drawPath(path);
}

/// A clock face with two hands.
void clock(QPainter& p) {
    p.drawEllipse(QPointF(12.0, 12.0), 9.0, 9.0);
    p.drawPolyline(QPolygonF({{12.0, 6.5}, {12.0, 12.0}, {16.0, 12.0}}));
}

/// A floppy disk: body with a clipped corner, shutter on top, label below.
/// @param p the painter
/// @param named whether the label carries an ellipsis — the "asks for a name" variant
void floppy(QPainter& p, bool named) {
    QPainterPath body;
    body.moveTo(3.0, 3.0);
    body.lineTo(16.0, 3.0);
    body.lineTo(21.0, 8.0);
    body.lineTo(21.0, 21.0);
    body.lineTo(3.0, 21.0);
    body.closeSubpath();
    p.drawPath(body);
    p.drawPolyline(QPolygonF({{8.0, 3.0}, {8.0, 8.5}, {15.0, 8.5}, {15.0, 3.0}}));
    p.drawPolyline(QPolygonF({{6.5, 21.0}, {6.5, 14.0}, {17.5, 14.0}, {17.5, 21.0}}));
    if (named) {
        p.setBrush(p.pen().brush());
        for (const qreal x : {9.5, 12.0, 14.5}) {
            p.drawEllipse(QPointF(x, 17.5), 0.85, 0.85);
        }
        p.setBrush(Qt::NoBrush);
    }
}

void disk(QPainter& p) {
    floppy(p, false);
}

void disk_named(QPainter& p) {
    floppy(p, true);
}

/// An arrow hooking back to the left.
void back_arrow(QPainter& p) {
    QPainterPath path;
    path.moveTo(19.0, 19.5);
    path.cubicTo(19.0, 10.0, 14.5, 7.5, 7.5, 7.5);
    p.drawPath(path);
    p.drawPolyline(QPolygonF({{11.5, 3.5}, {7.5, 7.5}, {11.5, 11.5}}));
}

/// The same arrow seen in a mirror. Drawing it a second time by hand would let the two drift apart
/// on the first edit.
void forward_arrow(QPainter& p) {
    p.save();
    p.translate(grid, 0.0);
    p.scale(-1.0, 1.0);
    back_arrow(p);
    p.restore();
}

/// A dashed frame around a plus: the marching ants that mean "take these and make them one".
void frame_with_plus(QPainter& p) {
    const QPen solid = p.pen();

    QPen dashed = solid;
    dashed.setWidthF(weight * 0.85);
    dashed.setCapStyle(Qt::FlatCap);
    // The pattern is in multiples of the pen width, so it stays proportional at every size.
    dashed.setDashPattern({2.6, 2.0});
    p.setPen(dashed);
    p.drawRoundedRect(QRectF(2.6, 4.6, 18.8, 14.8), 3.0, 3.0);

    p.setPen(solid);
    p.drawLine(QPointF(12.0, 8.6), QPointF(12.0, 15.4));
    p.drawLine(QPointF(8.6, 12.0), QPointF(15.4, 12.0));
}

// --- the engine ----------------------------------------------------------------------------------

/// An icon whose artwork is a function and whose colour is the palette's. Qt asks the engine for
/// every size, density and mode it needs, so one instance serves a 16 px menu, its HiDPI twin and
/// the greyed-out state of both — and a theme change is picked up on the next repaint, because the
/// colour is read here rather than baked into a pixmap.
class drawn_icon final : public QIconEngine {
   public:
    explicit drawn_icon(draw_fn draw) : draw_(draw) {}

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State /*state*/) override {
        const QPalette::ColorGroup group = mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active;
        QColor colour = QGuiApplication::palette().color(group, QPalette::WindowText);
        if (mode != QIcon::Disabled) {
            colour.setAlphaF(icon_alpha);
        }

        // A non-square rect would stretch the drawing, so the grid is fitted into the largest
        // square the rect holds and centred in it.
        const qreal side = std::min(rect.width(), rect.height());
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->translate(rect.x() + (rect.width() - side) / 2.0, rect.y() + (rect.height() - side) / 2.0);
        painter->scale(side / grid, side / grid);

        QPen pen(colour, weight);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        draw_(*painter);
        painter->restore();
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        return scaledPixmap(size, mode, state, 1.0);
    }

    // Menus ask for a pixmap rather than painting the icon in place, so this is the path that
    // actually runs. It is overridden because the base engine paints into a QPixmap it never
    // clears: line art over uninitialised memory is how an icon ends up with noise behind it.
    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state, qreal scale) override {
        QImage image(size, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        paint(&painter, QRect(QPoint(0, 0), size), mode, state);
        painter.end();

        // The size Qt asks for is already in device pixels; the ratio says how many of them go into
        // a logical one, and without it the icon would be drawn twice as large on a HiDPI screen.
        QPixmap pixmap = QPixmap::fromImage(image);
        pixmap.setDevicePixelRatio(scale);
        return pixmap;
    }

    [[nodiscard]] QIconEngine* clone() const override {
        return new drawn_icon(draw_);
    }

   private:
    draw_fn draw_;
};

/// Wraps a drawing into an icon. QIcon takes the engine over.
[[nodiscard]] QIcon drawn(draw_fn draw) {
    return QIcon(new drawn_icon(draw));
}

}  // namespace

QIcon brand() {
    // One .ico carries every size, and it is the same file the executable wears — a second copy of
    // the artwork for the window would be one more thing to keep in step. Qt's reader hands out one
    // frame at a time, so the rest are asked for explicitly; stopping at the first would leave the
    // icon with a single size to scale from.
    QIcon icon;
    QImageReader reader(QStringLiteral(":/atp.ico"));
    QImage frame;
    while (reader.read(&frame)) {
        icon.addPixmap(QPixmap::fromImage(frame));
        if (!reader.jumpToNextImage()) {
            break;
        }
    }
    return icon;
}

QIcon new_document() {
    return drawn(sheet);
}

QIcon open_document() {
    return drawn(folder);
}

QIcon recent() {
    return drawn(clock);
}

QIcon save() {
    return drawn(disk);
}

QIcon save_as() {
    return drawn(disk_named);
}

QIcon undo() {
    return drawn(back_arrow);
}

QIcon redo() {
    return drawn(forward_arrow);
}

QIcon new_group() {
    return drawn(frame_with_plus);
}

}  // namespace atp::studio::ui::icons
