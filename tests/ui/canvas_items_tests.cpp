// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <any>
#include <cmath>
#include <optional>
#include <string>
#include <typeindex>
#include <vector>

#include <gtest/gtest.h>

#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QGraphicsItem>
#include <QGraphicsSimpleTextItem>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QPointF>
#include <QRectF>

#include <atp/studio/languages.hpp>

#include "canvas/canvas_items.hpp"
#include "canvas/canvas_palette.hpp"
#include "kit/icons.hpp"
#include "ui/qt_app.hpp"

namespace {

using atp::studio::ui::canvas_colors;
using atp::studio::ui::canvas_palette;
using atp::studio::ui::glyph_item;
using atp::studio::ui::link_item;
using atp::studio::ui::node_item;
using atp::studio::ui::pin_item;
using atp::studio::ui::pin_tooltip;
using atp::studio::ui::stub_item;

constexpr bool as_input = false;
constexpr bool as_output = true;

QPalette light_scheme() {
    QPalette p;
    p.setColor(QPalette::Base, QColor(255, 255, 255));
    p.setColor(QPalette::Text, QColor(0, 0, 0));
    p.setColor(QPalette::Highlight, QColor(0, 120, 215));
    return p;
}

QPalette dark_scheme() {
    QPalette p;
    p.setColor(QPalette::Base, QColor(27, 27, 27));
    p.setColor(QPalette::Text, QColor(255, 255, 255));
    p.setColor(QPalette::Highlight, QColor(0, 120, 215));
    return p;
}

const canvas_palette& scheme() {
    static const canvas_palette colors = canvas_colors(dark_scheme());
    return colors;
}

TEST(UiCanvasItems, UniversalInputPinIsAHollowRing) {
    const pin_item pin(nullptr, "probe.in", as_input, std::type_index(typeid(std::any)), scheme());
    EXPECT_EQ(pin.brush().style(), Qt::NoBrush);
    EXPECT_GT(pin.pen().widthF(), 1.0);
}

TEST(UiCanvasItems, TypedPinIsFilledWithAColourOfItsOwn) {
    const pin_item number(nullptr, "sink.value", as_input, std::type_index(typeid(int)), scheme());
    const pin_item text(nullptr, "text_sink.value", as_input, std::type_index(typeid(std::string)), scheme());
    EXPECT_EQ(number.brush().style(), Qt::SolidPattern);
    EXPECT_EQ(text.brush().style(), Qt::SolidPattern);
    EXPECT_NE(number.brush().color(), text.brush().color());
}

TEST(UiCanvasItems, OnePortTypeIsOneColourWhicheverSideItIsOn) {
    const pin_item in(nullptr, "sink.value", as_input, std::type_index(typeid(int)), scheme());
    const pin_item out(nullptr, "source.value", as_output, std::type_index(typeid(int)), scheme());
    EXPECT_EQ(in.brush().color(), out.brush().color());
}

TEST(UiCanvasItems, UnknownTypeStaysFilledSoItIsNotReadAsUniversal) {
    const pin_item pin(nullptr, "gain.value", as_input, std::nullopt, scheme());
    EXPECT_EQ(pin.brush().style(), Qt::SolidPattern);
    EXPECT_FALSE(pin.port_type().has_value());
}

TEST(UiCanvasItems, AnyOutputIsNotMarkedAsUniversal) {
    const pin_item pin(nullptr, "source.out", as_output, std::type_index(typeid(std::any)), scheme());
    EXPECT_EQ(pin.brush().style(), Qt::SolidPattern);
}

TEST(UiCanvasItems, DimmingAPinIsReversible) {
    pin_item pin(nullptr, "sink.value", as_input, std::type_index(typeid(int)), scheme());
    EXPECT_DOUBLE_EQ(pin.opacity(), 1.0);
    pin.set_eligible(false);
    EXPECT_LT(pin.opacity(), 1.0);
    pin.set_eligible(true);
    EXPECT_DOUBLE_EQ(pin.opacity(), 1.0);
}

TEST(UiCanvasItems, HoverGrowsThePinAndLetsItBack) {
    pin_item pin(nullptr, "sink.value", as_input, std::type_index(typeid(int)), scheme());
    EXPECT_DOUBLE_EQ(pin.scale(), 1.0);
    pin.set_hovered(true);
    EXPECT_GT(pin.scale(), 1.0);
    pin.set_hovered(false);
    EXPECT_DOUBLE_EQ(pin.scale(), 1.0);
}

TEST(UiCanvasItems, HoverLeavesThePinCentreWhereItWas) {
    pin_item pin(nullptr, "sink.value", as_input, std::type_index(typeid(int)), scheme());
    pin.setPos(40.0, 60.0);
    const QPointF before = pin.mapToScene(pin.rect().center());
    pin.set_hovered(true);
    const QPointF after = pin.mapToScene(pin.rect().center());
    EXPECT_DOUBLE_EQ(after.x(), before.x());
    EXPECT_DOUBLE_EQ(after.y(), before.y());
}

TEST(UiCanvasItems, PinIsReadyToBeHovered) {
    const pin_item pin(nullptr, "sink.value", as_input, std::type_index(typeid(int)), scheme());
    EXPECT_TRUE(pin.acceptHoverEvents());
}

TEST(UiCanvasItems, TooltipSaysAUniversalInputTakesAnything) {
    const QString text = pin_tooltip("probe", std::type_index(typeid(std::any)), as_input);
    EXPECT_TRUE(text.startsWith(QStringLiteral("probe\n")));
    EXPECT_TRUE(text.contains(QStringLiteral("accepts any type")));
}

TEST(UiCanvasItems, TooltipSaysAnAnyOutputFitsAUniversalInputOnly) {
    const QString text = pin_tooltip("out", std::type_index(typeid(std::any)), as_output);
    EXPECT_TRUE(text.contains(QStringLiteral("fits a universal input")));
    EXPECT_FALSE(text.contains(QStringLiteral("accepts any type")));
}

TEST(UiCanvasItems, TooltipNamesTheReasonATypeIsUnknown) {
    const QString text = pin_tooltip("gain", std::nullopt, as_input);
    EXPECT_TRUE(text.startsWith(QStringLiteral("gain\n")));
    EXPECT_TRUE(text.contains(QStringLiteral("factory not loaded")));
}

TEST(UiCanvasItems, TooltipOfATypedPortCarriesTheTypeName) {
    const QString text = pin_tooltip("value", std::type_index(typeid(int)), as_input);
    EXPECT_TRUE(text.startsWith(QStringLiteral("value\n")));
    EXPECT_TRUE(text.contains(QString::fromUtf8(typeid(int).name())));
}

TEST(UiCanvasItems, ANodeIsFilledFromTheSchemeItWasGiven) {
    const canvas_palette light = canvas_colors(light_scheme());
    const canvas_palette dark = canvas_colors(dark_scheme());
    const node_item pale("a", false, 60.0, light);
    const node_item deep("a", false, 60.0, dark);
    EXPECT_EQ(pale.brush().color(), light.node_fill);
    EXPECT_EQ(deep.brush().color(), dark.node_fill);
    EXPECT_NE(pale.brush().color(), deep.brush().color());
}

TEST(UiCanvasItems, AGroupNodeIsFilledWithTheGroupColour) {
    const canvas_palette dark = canvas_colors(dark_scheme());
    const node_item group("g", true, 60.0, dark);
    EXPECT_EQ(group.brush().color(), dark.group_fill);
}

TEST(UiCanvasItems, TheDropFrameComesFromTheSchemeRatherThanTheApplication) {
    canvas_palette dark = canvas_colors(dark_scheme());
    dark.drop_target = QColor(1, 2, 3);
    node_item node("a", false, 60.0, dark);
    node.set_drop_target(true);
    EXPECT_EQ(node.pen().color(), QColor(1, 2, 3));
    node.set_drop_target(false);
    EXPECT_EQ(node.pen().color(), dark.node_border);
}

TEST(UiCanvasItems, APinIsOutlinedAndFilledFromTheScheme) {
    const canvas_palette light = canvas_colors(light_scheme());
    const pin_item pin(nullptr, "sink.value", as_input, std::type_index(typeid(int)), light);
    EXPECT_EQ(pin.pen().color(), light.pin_outline);
    EXPECT_EQ(pin.brush().color().lightness(), light.type_lightness);
    EXPECT_EQ(pin.brush().color().hslSaturation(), light.type_saturation);
}

TEST(UiCanvasItems, APinIsRingedInTheGroundOfTheNodeItSitsOn) {
    const canvas_palette dark = canvas_colors(dark_scheme());
    node_item group("g", true, 60.0, dark);
    node_item module("m", false, 60.0, dark);
    const pin_item on_group(&group, "g.in", as_input, std::type_index(typeid(int)), dark);
    const pin_item on_module(&module, "m.in", as_input, std::type_index(typeid(int)), dark);
    EXPECT_EQ(on_group.pen().color(), dark.group_fill);
    EXPECT_EQ(on_module.pen().color(), dark.node_fill);
    EXPECT_NE(on_group.pen().color(), on_module.pen().color());
}

TEST(UiCanvasItems, OnePortTypeChangesColourWithTheScheme) {
    const std::type_index type(typeid(int));
    const pin_item pale(nullptr, "sink.value", as_input, type, canvas_colors(light_scheme()));
    const pin_item deep(nullptr, "sink.value", as_input, type, canvas_colors(dark_scheme()));
    EXPECT_EQ(pale.brush().color().hslHue(), deep.brush().color().hslHue());
    EXPECT_NE(pale.brush().color(), deep.brush().color());
}

TEST(UiCanvasItems, AUniversalPinTakesTheSchemeInkRatherThanATypeColour) {
    const canvas_palette dark = canvas_colors(dark_scheme());
    const pin_item pin(nullptr, "probe.in", as_input, std::type_index(typeid(std::any)), dark);
    EXPECT_EQ(pin.brush().style(), Qt::NoBrush);
    EXPECT_EQ(pin.pen().color(), dark.universal_ink);
}

QRectF label_rect(const stub_item& stub) {
    for (QGraphicsItem* child : stub.childItems()) {
        if (auto* text = qgraphicsitem_cast<QGraphicsSimpleTextItem*>(child)) {
            return text->sceneBoundingRect();
        }
    }
    return {};
}

TEST(UiCanvasItems, AnExportedOutputReachesOutToTheRightAndNamesItselfThere) {
    (void)atp_ui_tests::ensure_app();
    const canvas_palette dark = canvas_colors(dark_scheme());
    const QPointF pin(atp::studio::ui::node_width, 40.0);

    stub_item stub(as_output, "measures", std::type_index(typeid(int)), dark);
    stub.set_anchor(pin);

    const QRectF drawn = stub.path().boundingRect();
    EXPECT_NEAR(drawn.width(), atp::studio::ui::stub_length, 8.0)
        << "the segment is short, not a line across the level";
    const QRectF label = label_rect(stub);
    ASSERT_FALSE(label.isEmpty());
    EXPECT_GE(label.left(), pin.x());
}

TEST(UiCanvasItems, AnExportedInputReachesOutToTheLeftAndNamesItselfThere) {
    (void)atp_ui_tests::ensure_app();
    const canvas_palette dark = canvas_colors(dark_scheme());
    const QPointF pin(200.0, 40.0);

    stub_item stub(as_input, "numbers", std::type_index(typeid(int)), dark);
    stub.set_anchor(pin);

    const QRectF drawn = stub.path().boundingRect();
    EXPECT_NEAR(drawn.width(), atp::studio::ui::stub_length, 8.0);
    const QRectF label = label_rect(stub);
    ASSERT_FALSE(label.isEmpty());
    EXPECT_LE(label.right(), pin.x());
}

TEST(UiCanvasItems, AStubIsPointedAtByItsHeadAndNotAlongItsWholeSpan) {
    (void)atp_ui_tests::ensure_app();
    const canvas_palette dark = canvas_colors(dark_scheme());
    const QPointF pin(180.0, 40.0);

    stub_item stub(as_output, "measures", std::type_index(typeid(int)), dark);
    stub.set_anchor(pin);

    const QPainterPath hit = stub.shape();
    EXPECT_FALSE(hit.contains(pin)) << "the pin belongs to the node, not to the stub";
    EXPECT_TRUE(hit.contains(pin + QPointF(atp::studio::ui::stub_length, 0.0)));
}

TEST(UiCanvasItems, AStubStillClaimsTheWholeSegmentItPaints) {
    (void)atp_ui_tests::ensure_app();
    const canvas_palette dark = canvas_colors(dark_scheme());
    const QPointF pin(180.0, 40.0);

    stub_item stub(as_output, "measures", std::type_index(typeid(int)), dark);
    stub.set_anchor(pin);

    const QRectF bounds = stub.boundingRect();
    EXPECT_TRUE(bounds.contains(stub.path().boundingRect()))
        << "a bounding rect narrower than the path leaves stale pixels and culls the line away";
    EXPECT_TRUE(bounds.contains(stub.shape().controlPointRect())) << "Qt requires shape() inside boundingRect()";
}

TEST(UiCanvasItems, AHotStubTakesTheFlowColourAndStaysDashed) {
    (void)atp_ui_tests::ensure_app();
    const canvas_palette dark = canvas_colors(dark_scheme());

    stub_item stub(as_output, "measures", std::type_index(typeid(int)), dark);
    const QColor idle = stub.pen().color();
    ASSERT_EQ(stub.pen().style(), Qt::DashLine);

    stub.set_hot(true);
    EXPECT_EQ(stub.pen().color(), dark.link_hot);
    EXPECT_GT(stub.pen().widthF(), 1.5);
    EXPECT_EQ(stub.pen().style(), Qt::DashLine) << "a hot stub is still not a link";

    stub.set_hot(false);
    EXPECT_EQ(stub.pen().color(), idle) << "the port type comes back the moment the flow pauses";
    EXPECT_DOUBLE_EQ(stub.pen().widthF(), 1.5);
}

TEST(UiCanvasItems, AHotLinkTakesTheFlowColourAndGoesBack) {
    (void)atp_ui_tests::ensure_app();
    const canvas_palette dark = canvas_colors(dark_scheme());
    link_item link(0, dark);
    EXPECT_EQ(link.pen().color(), dark.link);
    link.set_hot(true);
    EXPECT_EQ(link.pen().color(), dark.link_hot);
    EXPECT_GT(link.pen().widthF(), 1.5);
    link.set_hot(false);
    EXPECT_EQ(link.pen().color(), dark.link);
    EXPECT_DOUBLE_EQ(link.pen().widthF(), 1.5);
}

TEST(UiCanvasItems, AMarkKeepsTheInkItWasGivenRatherThanReadingAPalette) {
    (void)atp_ui_tests::ensure_app();
    const QColor ink(200, 30, 90);
    glyph_item glyph(nullptr, QStringLiteral(":/icons/module.svg"), ink, 14.0);

    EXPECT_EQ(glyph.ink(), ink);
    EXPECT_EQ(glyph.boundingRect(), QRectF(0.0, 0.0, 14.0, 14.0));
    EXPECT_TRUE(glyph.valid());
}

TEST(UiCanvasItems, TheMarkIsCentredOnTheCapitalsRatherThanOnTheEmBox) {
    (void)atp_ui_tests::ensure_app();
    const QFont font = QApplication::font();
    const QFontMetricsF metrics(font);
    const double text_top = 4.0;
    const atp::studio::ui::glyph_layout mark = atp::studio::ui::node_glyph_layout(font, text_top, 8.0);

    const double baseline = text_top + metrics.ascent();
    EXPECT_NEAR(mark.top + (mark.side / 2.0), baseline - (metrics.capHeight() / 2.0), 0.001);
    EXPECT_NEAR(mark.side, std::round(metrics.capHeight() * atp::studio::ui::node_glyph_over_cap), 0.001);
    EXPECT_NEAR(mark.text_left, 8.0 + mark.side + atp::studio::ui::node_glyph_gap, 0.001);
}

TEST(UiCanvasItems, TheMarksOfOneFamilyShareTheirLargestExtent) {
    (void)atp_ui_tests::ensure_app();
    const double side = 24.0;
    std::vector<double> extents;
    for (const char* artwork :
         {":/icons/module.svg", ":/icons/group.svg", ":/icons/script.svg", ":/icons/python.svg", ":/icons/lua.svg"}) {
        glyph_item glyph(nullptr, QString::fromLatin1(artwork), QColor(0, 0, 0), side);
        ASSERT_TRUE(glyph.valid()) << artwork;

        QImage surface(static_cast<int>(side), static_cast<int>(side), QImage::Format_ARGB32_Premultiplied);
        surface.fill(Qt::transparent);
        QPainter painter(&surface);
        glyph.paint(&painter, nullptr, nullptr);
        painter.end();

        int left = surface.width();
        int top = surface.height();
        int right = -1;
        int bottom = -1;
        for (int y = 0; y < surface.height(); ++y) {
            for (int x = 0; x < surface.width(); ++x) {
                if (qAlpha(surface.pixel(x, y)) == 0) {
                    continue;
                }
                left = std::min(left, x);
                top = std::min(top, y);
                right = std::max(right, x);
                bottom = std::max(bottom, y);
            }
        }
        ASSERT_GE(right, left) << artwork;
        extents.push_back(std::max(right - left, bottom - top) + 1.0);
    }
    for (double extent : extents) {
        EXPECT_NEAR(extent, extents.front(), 1.5);
    }
}

TEST(UiCanvasItems, PaintingTheMarkLeavesThePainterAsItFoundIt) {
    (void)atp_ui_tests::ensure_app();
    QImage surface(32, 32, QImage::Format_ARGB32_Premultiplied);
    surface.fill(Qt::transparent);
    QPainter painter(&surface);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

    glyph_item glyph(nullptr, QStringLiteral(":/icons/module.svg"), QColor(10, 20, 30), 14.0);
    glyph.paint(&painter, nullptr, nullptr);

    EXPECT_FALSE(painter.testRenderHint(QPainter::SmoothPixmapTransform));
}

TEST(UiCanvasItems, TheMarkDoesNotTakeThePressAwayFromTheNodeItSitsOn) {
    (void)atp_ui_tests::ensure_app();
    glyph_item glyph(nullptr, QStringLiteral(":/icons/module.svg"), QColor(0, 0, 0), 14.0);

    EXPECT_EQ(glyph.acceptedMouseButtons(), Qt::NoButton);
}

TEST(UiCanvasItems, ArtworkThatIsNotThereIsAnswerableRatherThanSilent) {
    (void)atp_ui_tests::ensure_app();
    glyph_item glyph(nullptr, QStringLiteral(":/icons/no_such_language.svg"), QColor(0, 0, 0), 14.0);

    EXPECT_FALSE(glyph.valid());
}

TEST(UiCanvasItems, EveryLanguageTheStudioAuthorsInHasArtworkThatParses) {
    (void)atp_ui_tests::ensure_app();
    for (const atp::studio::script_language& lang : atp::studio::languages()) {
        const QString artwork = atp::studio::ui::icons::script_artwork(lang.id);
        glyph_item glyph(nullptr, artwork, QColor(0, 0, 0), 14.0);
        EXPECT_TRUE(glyph.valid()) << artwork.toStdString();
    }
    glyph_item binary(nullptr, atp::studio::ui::icons::binary_module_artwork(), QColor(0, 0, 0), 14.0);
    EXPECT_TRUE(binary.valid());
    glyph_item group(nullptr, atp::studio::ui::icons::group_artwork(), QColor(0, 0, 0), 14.0);
    EXPECT_TRUE(group.valid());
}

TEST(UiCanvasItems, ALanguageWithNoArtworkOfItsOwnFallsBackToTheScriptSheet) {
    (void)atp_ui_tests::ensure_app();
    EXPECT_EQ(atp::studio::ui::icons::script_artwork("brainfuck"), QStringLiteral(":/icons/script.svg"));
    EXPECT_EQ(atp::studio::ui::icons::script_artwork(""), QStringLiteral(":/icons/script.svg"));

    glyph_item glyph(nullptr, atp::studio::ui::icons::script_artwork("brainfuck"), QColor(0, 0, 0), 14.0);
    EXPECT_TRUE(glyph.valid());
}

}  // namespace
