// SPDX-License-Identifier: Apache-2.0
#include "kit/icons.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

#include <QFile>
#include <QGuiApplication>
#include <QIconEngine>
#include <QImage>
#include <QImageReader>
#include <QPaintDevice>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QSvgRenderer>

#include <atp/studio/languages.hpp>

namespace atp::studio::ui::icons {
namespace {

/// How much quieter an icon is than the text it labels: at full strength a row of icons pulls the
/// eye away from the words, which are what the user is actually reading.
constexpr float icon_alpha = 0.8f;

/// An icon whose artwork is an SVG in the resources and whose colour is the palette's. Qt asks the
/// engine for every size, density and mode it needs, so one instance serves a 16 px menu, its HiDPI
/// twin and the greyed-out state of both — and a theme change is picked up on the next repaint,
/// because the colour is read here rather than baked into the file.
///
/// Every entry point renders at the resolution the destination really has, which for paint() means
/// asking the paint device for its devicePixelRatio rather than trusting the rectangle: that
/// rectangle is in logical pixels, and Qt's own QIcon::paint — the path a toolbar button and a menu
/// row both take — hands it straight over. Rendering at its size and letting the painter's transform
/// blow the result up is what made every icon on a scaled display look soft while the canvas, which
/// has always accounted for the ratio, stayed sharp.
class svg_icon final : public QIconEngine {
   public:
    explicit svg_icon(QString path) : path_(std::move(path)), renderer_(path_) {}

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State) override {
        const int side = std::min(rect.width(), rect.height());
        if (side <= 0) {
            return;
        }
        const qreal ratio =
            painter->device() != nullptr ? painter->device()->devicePixelRatio() : qGuiApp->devicePixelRatio();
        const int pixels = std::max(1, qRound(side * ratio));
        const QRectF at(QPointF(rect.x() + ((rect.width() - side) / 2.0), rect.y() + ((rect.height() - side) / 2.0)),
                        QSizeF(side, side));
        painter->drawImage(at, render(QSize(pixels, pixels), mode));
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

/// The file inside the resource directory, e.g. "save.svg", as a resource path.
[[nodiscard]] QString resource_path(std::string_view name) {
    QString path = QStringLiteral(":/icons/");
    path += QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
    return path;
}

/// Wraps a file in :/icons into an icon. QIcon takes the engine over.
/// @param name file name inside the resource directory, e.g. "save.svg"
[[nodiscard]] QIcon from_resource(const char* name) {
    return QIcon(new svg_icon(resource_path(name)));
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

QIcon run() {
    return from_resource("run.svg");
}

QIcon stop() {
    return from_resource("stop.svg");
}

QIcon attach() {
    return from_resource("attach.svg");
}

QIcon soft_wrap() {
    return from_resource("soft_wrap.svg");
}

QIcon scroll_to_end() {
    return from_resource("scroll_to_end.svg");
}

QIcon clear_all() {
    return from_resource("clear_all.svg");
}

QIcon open_view() {
    return from_resource("open_view.svg");
}

QIcon close_tab() {
    return from_resource("close_tab.svg");
}

QString binary_module_artwork() {
    return resource_path("module.svg");
}

QString script_artwork(std::string_view language_id) {
    if (language_id.empty()) {
        return resource_path("script.svg");
    }
    const QString own = resource_path(std::string(language_id) + ".svg");
    return QFile::exists(own) ? own : resource_path("script.svg");
}

QString group_artwork() {
    return resource_path("group.svg");
}

QIcon module_icons::of_source(std::string_view source) {
    const script_language* lang = studio::language_of_source(source);
    const QString path =
        source.empty() ? binary_module_artwork() : script_artwork(lang != nullptr ? lang->id : std::string_view());
    const std::string key = path.toStdString();
    auto it = kept_.find(key);
    if (it == kept_.end()) {
        it = kept_.emplace(key, QIcon(new svg_icon(path))).first;
    }
    return it->second;
}

}  // namespace atp::studio::ui::icons
