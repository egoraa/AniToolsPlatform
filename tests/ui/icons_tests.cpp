// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <string>

#include <gtest/gtest.h>

#include <QDir>
#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QSvgRenderer>

#include <atp/studio/languages.hpp>

#include "kit/icons.hpp"
#include "ui/qt_app.hpp"

namespace {

using atp::studio::ui::icons::module_icons;

QImage drawn(const QIcon& icon) {
    return icon.pixmap(32, 32).toImage();
}

/// Side of the square an icon is measured on. Large enough that a stroke of the family is many
/// pixels wide, so the answer is the artwork's and not the rasteriser's.
constexpr int measure_side = 96;

/// The artwork of one file, rendered over the whole square its viewBox declares.
QImage artwork(const QString& name) {
    QImage image(measure_side, measure_side, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QSvgRenderer renderer(QStringLiteral(":/icons/") + name);
    if (!renderer.isValid()) {
        return {};
    }
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    renderer.render(&painter, QRectF(0.0, 0.0, measure_side, measure_side));
    painter.end();
    return image;
}

/// What the drawing actually covers, as opposed to what its viewBox claims. The threshold keeps the
/// antialiased fringe of a stroke out of the answer, which would otherwise add a pixel on each side.
QRect ink_of(const QImage& image) {
    constexpr int faint = 16;
    int left = image.width();
    int top = image.height();
    int right = -1;
    int bottom = -1;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) < faint) {
                continue;
            }
            left = std::min(left, x);
            top = std::min(top, y);
            right = std::max(right, x);
            bottom = std::max(bottom, y);
        }
    }
    return right < 0 ? QRect() : QRect(QPoint(left, top), QPoint(right, bottom));
}

TEST(UiIcons, APanelMarksAScriptDifferentlyFromAPlugin) {
    (void)atp_ui_tests::ensure_app();
    module_icons kept;

    const QImage binary = drawn(kept.of_source(""));
    ASSERT_FALSE(binary.isNull());
    EXPECT_NE(binary, drawn(kept.of_source("/home/user/modules/my_filter.py")));
    EXPECT_NE(binary, drawn(kept.of_source("/home/user/modules/gain_stage.lua")));
    EXPECT_NE(binary, drawn(kept.of_source("/home/user/modules/exotic.rb")));
}

TEST(UiIcons, EveryLanguageIsMarkedApartFromTheOthers) {
    (void)atp_ui_tests::ensure_app();
    module_icons kept;

    QImage previous = drawn(kept.of_source(""));
    for (const atp::studio::script_language& lang : atp::studio::languages()) {
        const QImage own = drawn(kept.of_source("module" + std::string(lang.file_extension)));
        ASSERT_FALSE(own.isNull()) << lang.id;
        EXPECT_NE(previous, own) << lang.id;
        previous = own;
    }
}

TEST(UiIcons, TheSameKindOfModuleAlwaysGetsTheSameMark) {
    (void)atp_ui_tests::ensure_app();
    module_icons kept;

    EXPECT_EQ(drawn(kept.of_source("/a/one.py")), drawn(kept.of_source("/b/other.py")));
    EXPECT_EQ(drawn(kept.of_source("")), drawn(kept.of_source("")));
}

TEST(UiIcons, ASourceOfAnUnknownKindIsMarkedAsAScriptAndNotAsAPlugin) {
    (void)atp_ui_tests::ensure_app();
    module_icons kept;

    EXPECT_EQ(drawn(kept.of_source("/modules/exotic.rb")), drawn(kept.of_source("/modules/other.tcl")));
    EXPECT_NE(drawn(kept.of_source("/modules/exotic.rb")), drawn(kept.of_source("")));
}

TEST(UiIcons, TheWholeFamilyIsDrawnOnOneGrid) {
    (void)atp_ui_tests::ensure_app();
    const QStringList files = QDir(QStringLiteral(":/icons")).entryList({QStringLiteral("*.svg")}, QDir::Files);
    ASSERT_FALSE(files.isEmpty()) << "the artwork rides on atp_studio_ui, so the suite sees it";

    for (const QString& name : files) {
        const QImage image = artwork(name);
        ASSERT_FALSE(image.isNull()) << name.toStdString();
        const QRect ink = ink_of(image);
        ASSERT_FALSE(ink.isEmpty()) << name.toStdString();
        const double fill = std::max(ink.width(), ink.height()) / static_cast<double>(measure_side);
        EXPECT_GE(fill, 0.72) << name.toStdString() << " is drawn smaller than the family";
        EXPECT_LE(fill, 0.86) << name.toStdString() << " is drawn larger than the family";
    }
}

}  // namespace
