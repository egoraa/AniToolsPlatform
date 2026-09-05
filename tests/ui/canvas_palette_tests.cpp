// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <cmath>

#include <gtest/gtest.h>

#include <QColor>
#include <QPalette>

#include "canvas/canvas_palette.hpp"

namespace {

using atp::studio::ui::canvas_colors;
using atp::studio::ui::canvas_palette;

constexpr double text_contrast = 4.5;
constexpr double object_contrast = 3.0;
constexpr double tellable_apart = 1.5;

/// The one ceiling in this file: the grid must stay under it, where everything else has a floor
/// to clear. A grid as loud as a connection stops being a measure and becomes clutter.
constexpr double quiet_contrast = 1.6;

double channel(double srgb) {
    return srgb <= 0.03928 ? srgb / 12.92 : std::pow((srgb + 0.055) / 1.055, 2.4);
}

double luminance(const QColor& c) {
    return (0.2126 * channel(static_cast<double>(c.redF()))) + (0.7152 * channel(static_cast<double>(c.greenF()))) +
           (0.0722 * channel(static_cast<double>(c.blueF())));
}

double contrast(const QColor& a, const QColor& b) {
    const double la = luminance(a);
    const double lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

QPalette light_scheme() {
    QPalette p;
    p.setColor(QPalette::Base, QColor(255, 255, 255));
    p.setColor(QPalette::Window, QColor(240, 240, 240));
    p.setColor(QPalette::Text, QColor(0, 0, 0));
    p.setColor(QPalette::Highlight, QColor(0, 120, 215));
    return p;
}

QPalette dark_scheme() {
    QPalette p;
    p.setColor(QPalette::Base, QColor(27, 27, 27));
    p.setColor(QPalette::Window, QColor(32, 32, 32));
    p.setColor(QPalette::Text, QColor(255, 255, 255));
    p.setColor(QPalette::Highlight, QColor(0, 120, 215));
    return p;
}

TEST(UiCanvasPalette, TheNodeFollowsTheSchemeOfTheCanvas) {
    EXPECT_GT(canvas_colors(light_scheme()).node_fill.lightness(), 128);
    EXPECT_LT(canvas_colors(dark_scheme()).node_fill.lightness(), 128);
}

TEST(UiCanvasPalette, AGroupStaysTellableFromAModule) {
    for (const QPalette& p : {light_scheme(), dark_scheme()}) {
        const canvas_palette c = canvas_colors(p);
        EXPECT_NE(c.node_fill, c.group_fill);
        EXPECT_NE(c.node_fill.hslHue(), c.group_fill.hslHue());
    }
}

TEST(UiCanvasPalette, TheTitleAndThePortLabelAreReadableOnTheNode) {
    for (const QPalette& p : {light_scheme(), dark_scheme()}) {
        const canvas_palette c = canvas_colors(p);
        EXPECT_GE(contrast(c.node_title, c.node_fill), text_contrast);
        EXPECT_GE(contrast(c.node_title, c.group_fill), text_contrast);
        EXPECT_GE(contrast(c.port_label, c.node_fill), text_contrast);
    }
}

TEST(UiCanvasPalette, TheFactoryNameAndTheBrokenNoteAreReadableOnTheNode) {
    for (const QPalette& p : {light_scheme(), dark_scheme()}) {
        const canvas_palette c = canvas_colors(p);
        EXPECT_GE(contrast(c.node_subtitle, c.node_fill), text_contrast);
        EXPECT_GE(contrast(c.node_alert, c.node_fill), text_contrast);
    }
}

TEST(UiCanvasPalette, AStubLabelIsJudgedAgainstTheCanvasAndNotAgainstTheNode) {
    for (const QPalette& p : {light_scheme(), dark_scheme()}) {
        EXPECT_GE(contrast(canvas_colors(p).stub_label, p.color(QPalette::Base)), text_contrast);
    }
}

TEST(UiCanvasPalette, AMonitoringLabelIsReadableOnTheCanvas) {
    for (const QPalette& p : {light_scheme(), dark_scheme()}) {
        EXPECT_GE(contrast(canvas_colors(p).link_label, p.color(QPalette::Base)), text_contrast);
    }
}

TEST(UiCanvasPalette, EveryLineIsVisibleAgainstTheCanvas) {
    for (const QPalette& p : {light_scheme(), dark_scheme()}) {
        const canvas_palette c = canvas_colors(p);
        const QColor base = p.color(QPalette::Base);
        EXPECT_GE(contrast(c.link, base), object_contrast);
        EXPECT_GE(contrast(c.link_hot, base), object_contrast);
        EXPECT_GE(contrast(c.drag_line, base), object_contrast);
        EXPECT_GE(contrast(c.node_border, base), object_contrast);
    }
}

TEST(UiCanvasPalette, AHotLinkIsTellableFromAnIdleOneWithoutItsColour) {
    for (const QPalette& p : {light_scheme(), dark_scheme()}) {
        const canvas_palette c = canvas_colors(p);
        EXPECT_GE(contrast(c.link_hot, c.link), tellable_apart);
    }
}

TEST(UiCanvasPalette, AUniversalRingIsVisibleOnBothTheNodeAndTheCanvas) {
    for (const QPalette& p : {light_scheme(), dark_scheme()}) {
        const canvas_palette c = canvas_colors(p);
        EXPECT_GE(contrast(c.universal_ink, c.node_fill), object_contrast);
        EXPECT_GE(contrast(c.universal_ink, p.color(QPalette::Base)), object_contrast);
    }
}

TEST(UiCanvasPalette, APinOfAnyHueIsVisibleOnEveryGroundItLandsOn) {
    for (const QPalette& p : {light_scheme(), dark_scheme()}) {
        const canvas_palette c = canvas_colors(p);
        for (int hue = 0; hue < 360; hue += 15) {
            const QColor fill = QColor::fromHsl(hue, c.type_saturation, c.type_lightness);
            EXPECT_GE(contrast(fill, c.node_fill), object_contrast) << "hue " << hue;
            EXPECT_GE(contrast(fill, c.group_fill), object_contrast) << "hue " << hue;
            EXPECT_GE(contrast(fill, p.color(QPalette::Base)), object_contrast) << "hue " << hue;
        }
    }
}

TEST(UiCanvasPalette, TheGridStaysQuieterThanAnythingDrawnOnIt) {
    for (const QPalette& p : {light_scheme(), dark_scheme()}) {
        const canvas_palette c = canvas_colors(p);
        const QColor base = p.color(QPalette::Base);
        EXPECT_LE(contrast(c.grid_line, base), quiet_contrast);
        EXPECT_LE(contrast(c.grid_line_major, base), quiet_contrast);
        EXPECT_GT(contrast(c.grid_line_major, base), contrast(c.grid_line, base));
        EXPECT_LT(contrast(c.grid_line, base), contrast(c.link, base));
    }
}

TEST(UiCanvasPalette, TheDropFrameIsTheOneTheProjectTreeUses) {
    const QPalette p = dark_scheme();
    EXPECT_EQ(canvas_colors(p).drop_target, p.color(QPalette::Highlight));
}

}  // namespace
