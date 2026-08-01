#include <any>
#include <optional>
#include <string>
#include <typeindex>

#include <gtest/gtest.h>

#include <QBrush>
#include <QPen>
#include <QPointF>

#include "canvas/canvas_items.hpp"

namespace {

using atp::studio::ui::pin_item;
using atp::studio::ui::pin_tooltip;

constexpr bool as_input = false;
constexpr bool as_output = true;

TEST(UiCanvasItems, UniversalInputPinIsAHollowRing) {
    const pin_item pin(nullptr, "probe.in", as_input, std::type_index(typeid(std::any)));
    EXPECT_EQ(pin.brush().style(), Qt::NoBrush);
    EXPECT_GT(pin.pen().widthF(), 1.0);
}

TEST(UiCanvasItems, TypedPinIsFilledWithAColourOfItsOwn) {
    const pin_item number(nullptr, "sink.value", as_input, std::type_index(typeid(int)));
    const pin_item text(nullptr, "text_sink.value", as_input, std::type_index(typeid(std::string)));
    EXPECT_EQ(number.brush().style(), Qt::SolidPattern);
    EXPECT_EQ(text.brush().style(), Qt::SolidPattern);
    EXPECT_NE(number.brush().color(), text.brush().color());
}

TEST(UiCanvasItems, OnePortTypeIsOneColourWhicheverSideItIsOn) {
    const pin_item in(nullptr, "sink.value", as_input, std::type_index(typeid(int)));
    const pin_item out(nullptr, "source.value", as_output, std::type_index(typeid(int)));
    EXPECT_EQ(in.brush().color(), out.brush().color());
}

TEST(UiCanvasItems, UnknownTypeStaysFilledSoItIsNotReadAsUniversal) {
    const pin_item pin(nullptr, "gain.value", as_input, std::nullopt);
    EXPECT_EQ(pin.brush().style(), Qt::SolidPattern);
    EXPECT_FALSE(pin.port_type().has_value());
}

TEST(UiCanvasItems, AnyOutputIsNotMarkedAsUniversal) {
    const pin_item pin(nullptr, "source.out", as_output, std::type_index(typeid(std::any)));
    EXPECT_EQ(pin.brush().style(), Qt::SolidPattern);
}

TEST(UiCanvasItems, DimmingAPinIsReversible) {
    pin_item pin(nullptr, "sink.value", as_input, std::type_index(typeid(int)));
    EXPECT_DOUBLE_EQ(pin.opacity(), 1.0);
    pin.set_eligible(false);
    EXPECT_LT(pin.opacity(), 1.0);
    pin.set_eligible(true);
    EXPECT_DOUBLE_EQ(pin.opacity(), 1.0);
}

TEST(UiCanvasItems, HoverGrowsThePinAndLetsItBack) {
    pin_item pin(nullptr, "sink.value", as_input, std::type_index(typeid(int)));
    EXPECT_DOUBLE_EQ(pin.scale(), 1.0);
    pin.set_hovered(true);
    EXPECT_GT(pin.scale(), 1.0);
    pin.set_hovered(false);
    EXPECT_DOUBLE_EQ(pin.scale(), 1.0);
}

TEST(UiCanvasItems, HoverLeavesThePinCentreWhereItWas) {
    pin_item pin(nullptr, "sink.value", as_input, std::type_index(typeid(int)));
    pin.setPos(40.0, 60.0);
    const QPointF before = pin.mapToScene(pin.rect().center());
    pin.set_hovered(true);
    const QPointF after = pin.mapToScene(pin.rect().center());
    EXPECT_DOUBLE_EQ(after.x(), before.x());
    EXPECT_DOUBLE_EQ(after.y(), before.y());
}

TEST(UiCanvasItems, PinIsReadyToBeHovered) {
    const pin_item pin(nullptr, "sink.value", as_input, std::type_index(typeid(int)));
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

}  // namespace
