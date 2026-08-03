#include "kit/icons.hpp"

#include <algorithm>
#include <utility>

#include <QGuiApplication>
#include <QIconEngine>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QString>
#include <QSvgRenderer>

namespace atp::studio::ui::icons {
namespace {

/// How much quieter an icon is than the text it labels: at full strength a row of icons pulls the
/// eye away from the words, which are what the user is actually reading.
constexpr float icon_alpha = 0.8f;

/// An icon whose artwork is an SVG in the resources and whose colour is the palette's. Qt asks the
/// engine for every size, density and mode it needs, so one instance serves a 16 px menu, its HiDPI
/// twin and the greyed-out state of both — and a theme change is picked up on the next repaint,
/// because the colour is read here rather than baked into the file.
class svg_icon final : public QIconEngine {
   public:
    explicit svg_icon(QString path) : path_(std::move(path)), renderer_(path_) {}

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State) override {
        const int side = std::min(rect.width(), rect.height());
        if (side <= 0) {
            return;
        }
        const QPoint at(rect.x() + ((rect.width() - side) / 2), rect.y() + ((rect.height() - side) / 2));
        painter->drawImage(at, render(QSize(side, side), mode));
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State, qreal scale) override {
        const int side = std::min(size.width(), size.height());
        if (side <= 0) {
            return {};
        }
        QPixmap pixmap = QPixmap::fromImage(render(QSize(side, side), mode));
        pixmap.setDevicePixelRatio(scale);
        return pixmap;
    }

    [[nodiscard]] QIconEngine* clone() const override {
        return new svg_icon(path_);
    }

   private:
    /// The artwork at one size, in the colour the palette gives text.
    QImage render(const QSize& size, QIcon::Mode mode) {
        QImage image(size, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);

        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        renderer_.render(&painter, QRectF(QPointF(0.0, 0.0), QSizeF(size)));

        const QPalette::ColorGroup group = mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active;
        QColor colour = QGuiApplication::palette().color(group, QPalette::WindowText);
        if (mode != QIcon::Disabled) {
            colour.setAlphaF(icon_alpha);
        }
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(image.rect(), colour);
        painter.end();
        return image;
    }

    QString path_;
    QSvgRenderer renderer_;
};

/// Wraps a file in :/icons into an icon. QIcon takes the engine over.
/// @param name file name inside the resource directory, e.g. "save.svg"
[[nodiscard]] QIcon from_resource(const char* name) {
    QString path = QStringLiteral(":/icons/");
    path += QLatin1String(name);
    return QIcon(new svg_icon(path));
}

}  // namespace

QIcon brand() {
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

QIcon new_project() {
    return from_resource("new_project.svg");
}

QIcon open_project() {
    return from_resource("open_project.svg");
}

QIcon recent() {
    return from_resource("recent.svg");
}

QIcon save() {
    return from_resource("save.svg");
}

QIcon save_as() {
    return from_resource("save_as.svg");
}

QIcon undo() {
    return from_resource("undo.svg");
}

QIcon redo() {
    return from_resource("redo.svg");
}

QIcon new_group() {
    return from_resource("new_group.svg");
}

QIcon group() {
    return from_resource("group.svg");
}

QIcon module() {
    return from_resource("module.svg");
}

QIcon plugin() {
    return from_resource("plugin.svg");
}

QIcon directory() {
    return from_resource("directory.svg");
}

}  // namespace atp::studio::ui::icons
