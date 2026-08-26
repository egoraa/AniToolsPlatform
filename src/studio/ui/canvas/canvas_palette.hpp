// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_UI_CANVAS_PALETTE_HPP
#define ATP_STUDIO_UI_CANVAS_PALETTE_HPP

#include <QColor>
#include <QPalette>

namespace atp::studio::ui {

/// Colours the canvas paints itself with.
///
/// The canvas is not a widget and takes nothing from the style: it draws rectangles and text on a
/// scene, so every colour it uses is one it chose, and a colour scheme has to be handed to it. That
/// is also why the two grounds are named apart. What sits on the node body — the name, the factory,
/// a port label — is judged against node_fill; what sits on the empty canvas — the alias of an
/// exposed port, a connection, a monitoring value — is judged against QPalette::Base. Painting the
/// second in the ink of the first is exactly how the light scheme used to end up with labels at two
/// to one against white.
struct canvas_palette {
    /// Body of a module node.
    QColor node_fill;

    /// Body of a subgroup node, a hue and a saturation away from a module's so that the two kinds of
    /// node never read alike.
    QColor group_fill;

    /// Outline of a node. Contrasts with the canvas rather than with the node: it is the edge
    /// between the two that it has to make visible.
    QColor node_border;

    /// Frame of the group a drop would land in: the palette's own highlight, so that the canvas and
    /// the project tree answer the same question in the same colour.
    QColor drop_target;

    /// Node name, on the node body.
    QColor node_title;

    /// Factory name under the node name, on the node body.
    QColor node_subtitle;

    /// The note that a node's factory is not loaded. Derived rather than taken from
    /// style::error_ink, which is one constant chosen against a panel background: that same red on a
    /// dark node body falls under three to one.
    QColor node_alert;

    /// Port name, on the node body.
    QColor port_label;

    /// Ring around a port pin with no node under it. A pin that has one is ringed in **that node's**
    /// own fill, module or subgroup, so that it reads as a hole punched in the body with the type
    /// colour showing through — this field is only the fallback, and it is node_fill because a pin
    /// without a parent is drawn nowhere in particular. Deriving the ring from the ink instead would
    /// put a light ring around a light pin in the dark scheme, where the pin is the light thing on
    /// the node.
    QColor pin_outline;

    /// Mark of a universal input (input<std::any>) and of a stub carrying one: it has no type, so it
    /// gets no type colour. Judged against the canvas, where a stub is, and against the node, where
    /// the pin is — the two grounds sit on the same side of the scheme, so one ink serves both.
    QColor universal_ink;

    /// A connection at rest.
    QColor link;

    /// A connection that carried a value since the last poll.
    QColor link_hot;

    /// The value printed beside a hot connection.
    QColor link_label;

    /// Alias of an exposed group port, printed on the canvas beside its stub arrow.
    QColor stub_label;

    /// The line drawn from a pin while a connection is being dragged.
    QColor drag_line;

    /// A line of the background grid.
    ///
    /// The grid is the one thing on the canvas with an **upper** bound on its contrast rather than a
    /// lower one: it is there to give the eye a sense of distance and of where a node was dropped,
    /// and the moment it is as loud as a connection it competes with the content it exists to
    /// measure. The test pins that ceiling, and it is the only ceiling in the set.
    QColor grid_line;

    /// Every fifth line of the grid, a shade stronger so that the spacing can be counted at a glance
    /// without any line becoming loud.
    QColor grid_line_major;

    /// Saturation a port type's colour is built at. The hue is the type's own; how vivid and how
    /// light it may be is the scheme's business, since a colour picked to sit on a dark node washes
    /// out on a pale one.
    int type_saturation;

    /// Lightness a port type's colour is built at. HSL rather than HSV, and this is the reason: at a
    /// fixed value a blue pin is far darker than a yellow one, so no single pair of grounds fits
    /// every hue. At a fixed lightness every type is equally visible on the node and off it.
    int type_lightness;
};

/// Derives the canvas colours from a widget palette.
///
/// The scheme is read from QPalette::Base — the ground the scene is drawn on — rather than from
/// Window: a style may darken one without the other, and Base is the one the labels have to survive.
/// @param p palette of the widget holding the canvas
/// @return the colours, complete; no field is left for a caller to fill in
[[nodiscard]] canvas_palette canvas_colors(const QPalette& p);

}  // namespace atp::studio::ui

#endif
