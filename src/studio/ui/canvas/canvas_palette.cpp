// SPDX-License-Identifier: Apache-2.0
#include "canvas/canvas_palette.hpp"

namespace atp::studio::ui {
namespace {

constexpr int module_hue = 225;
constexpr int group_hue = 260;
constexpr int flow_hue = 120;
constexpr int alert_hue = 0;
constexpr int drag_hue = 48;

constexpr int light_threshold = 128;

QColor mix(const QColor& from, const QColor& to, double t) {
    return {static_cast<int>(from.red() + ((to.red() - from.red()) * t)),
            static_cast<int>(from.green() + ((to.green() - from.green()) * t)),
            static_cast<int>(from.blue() + ((to.blue() - from.blue()) * t))};
}

QColor ink_for(const QColor& ground) {
    return ground.lightness() < light_threshold ? QColor(238, 239, 245) : QColor(24, 26, 33);
}

}  // namespace

canvas_palette canvas_colors(const QPalette& p) {
    const QColor base = p.color(QPalette::Base);
    const bool dark = base.lightness() < light_threshold;

    const int fill_saturation = dark ? 36 : 60;
    const int fill_lightness = dark ? 56 : 232;

    canvas_palette c;
    c.node_fill = QColor::fromHsl(module_hue, fill_saturation, fill_lightness);
    c.group_fill = QColor::fromHsl(group_hue, fill_saturation + 24, fill_lightness + 5);
    c.drop_target = p.color(QPalette::Highlight);

    const QColor node_ink = ink_for(c.node_fill);
    c.node_border = mix(c.node_fill, node_ink, 0.50);
    c.node_title = node_ink;
    c.node_subtitle = mix(c.node_fill, node_ink, 0.70);
    c.port_label = mix(c.node_fill, node_ink, 0.88);
    c.node_alert = QColor::fromHsl(alert_hue, 150, dark ? 187 : 95);
    c.pin_outline = c.node_fill;

    const QColor canvas_ink = ink_for(base);
    c.stub_label = mix(base, canvas_ink, 0.80);
    c.link = mix(base, canvas_ink, 0.55);
    c.link_hot = QColor::fromHsl(flow_hue, 200, dark ? 165 : 62);
    c.link_label = QColor::fromHsl(flow_hue, 200, dark ? 190 : 62);
    c.drag_line = QColor::fromHsl(drag_hue, 180, dark ? 175 : 95);
    c.universal_ink = mix(base, canvas_ink, 0.85);
    c.grid_line = mix(base, canvas_ink, 0.08);
    c.grid_line_major = mix(base, canvas_ink, 0.15);

    c.type_saturation = dark ? 215 : 235;
    c.type_lightness = dark ? 195 : 60;
    return c;
}

}  // namespace atp::studio::ui
