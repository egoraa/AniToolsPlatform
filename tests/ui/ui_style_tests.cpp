// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <cmath>

#include <gtest/gtest.h>

#include <QApplication>
#include <QBoxLayout>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QToolButton>
#include <QWidget>

#include "kit/icons.hpp"
#include "kit/ui_style.hpp"
#include "ui/qt_app.hpp"

namespace {

namespace style = atp::studio::ui::style;
namespace icons = atp::studio::ui::icons;

constexpr double text_contrast = 4.5;

double channel(double srgb) {
    return srgb <= 0.03928 ? srgb / 12.92 : std::pow((srgb + 0.055) / 1.055, 2.4);
}

double luminance(const QColor& c) {
    return (0.2126 * channel(c.redF())) + (0.7152 * channel(c.greenF())) + (0.0722 * channel(c.blueF()));
}

double contrast(const QColor& a, const QColor& b) {
    const double la = luminance(a);
    const double lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

QPalette scheme_with(const QColor& base) {
    QPalette p;
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::Window, base);
    return p;
}

int marks_on(const QWidget& w) {
    return static_cast<int>(w.findChildren<QWidget*>(QStringLiteral("error.frame"), Qt::FindDirectChildrenOnly).size());
}

TEST(UiStyle, TheRefusalColourIsReadableOnEitherScheme) {
    for (const QColor& base : {QColor(255, 255, 255), QColor(27, 27, 27)}) {
        const QPalette p = scheme_with(base);
        EXPECT_GE(contrast(style::error_ink(p), base), text_contrast) << "base lightness " << base.lightness();
    }
}

TEST(UiStyle, TheRefusalColourFollowsTheScheme) {
    const QColor pale = style::error_ink(scheme_with(QColor(255, 255, 255)));
    const QColor deep = style::error_ink(scheme_with(QColor(27, 27, 27)));
    EXPECT_NE(pale, deep) << "one red cannot be readable on both grounds";
}

TEST(UiStyle, MarkingAnErrorLeavesNoStyleSheetAndDoesNotTouchThePalette) {
    (void)atp_ui_tests::ensure_app();
    QLineEdit field;
    const QPalette before = field.palette();

    style::mark_error(&field, QStringLiteral("no"));

    EXPECT_EQ(marks_on(field), 1);
    EXPECT_EQ(field.toolTip(), QStringLiteral("no"));
    EXPECT_TRUE(field.styleSheet().isEmpty())
        << "a style sheet border switches a line edit off the native rendering entirely";
    EXPECT_EQ(field.palette().color(QPalette::Base), before.color(QPalette::Base))
        << "writing the resolved palette back would pin every role and stop the field following the theme";
}

TEST(UiStyle, MarkingTwiceLeavesOneMarkAndOnlyReplacesTheReason) {
    (void)atp_ui_tests::ensure_app();
    QLineEdit field;

    style::mark_error(&field, QStringLiteral("first"));
    for (int i = 0; i < 15; ++i) {
        style::mark_error(&field, QStringLiteral("again"));
    }

    EXPECT_EQ(marks_on(field), 1) << "a mark that compounds walks the editor towards solid red";
    EXPECT_EQ(field.toolTip(), QStringLiteral("again"));
}

TEST(UiStyle, EveryKindOfEditorCanCarryTheMark) {
    (void)atp_ui_tests::ensure_app();
    QLineEdit line;
    QComboBox box;
    QCheckBox check;

    style::mark_error(&line, QStringLiteral("no"));
    style::mark_error(&box, QStringLiteral("no"));
    style::mark_error(&check, QStringLiteral("no"));

    EXPECT_EQ(marks_on(line), 1);
    EXPECT_EQ(marks_on(box), 1) << "a non-editable combo box paints from Button, so a tint of Base left it unmarked";
    EXPECT_EQ(marks_on(check), 1);
}

TEST(UiStyle, ClearingAnErrorPutsTheEditorBack) {
    (void)atp_ui_tests::ensure_app();
    QLineEdit field;

    style::mark_error(&field, QStringLiteral("no"));
    style::clear_error(&field);

    EXPECT_EQ(marks_on(field), 0);
    EXPECT_TRUE(field.toolTip().isEmpty());
}

TEST(UiStyle, ClearingAWidgetThatWasNeverMarkedIsHarmless) {
    (void)atp_ui_tests::ensure_app();
    QLineEdit field;

    style::clear_error(&field);

    EXPECT_EQ(marks_on(field), 0);
}

TEST(UiStyle, ErrorTextPinsTheColourAndNothingElse) {
    (void)atp_ui_tests::ensure_app();
    QLabel label(QStringLiteral("refused"));
    label.setPalette(scheme_with(QColor(255, 255, 255)));

    style::error_text(&label);

    EXPECT_EQ(label.palette().color(QPalette::WindowText), style::error_ink(scheme_with(QColor(255, 255, 255))));
    EXPECT_FALSE(label.palette().isBrushSet(QPalette::Active, QPalette::Base))
        << "pinning every role is what stops a widget following the theme";
}

TEST(UiStyle, ErrorTextIsReReadWhenTheSchemeChanges) {
    (void)atp_ui_tests::ensure_app();
    QWidget host;
    host.setPalette(scheme_with(QColor(255, 255, 255)));
    QLabel label(QStringLiteral("refused"), &host);

    style::error_text(&label);
    const QColor pale = label.palette().color(QPalette::WindowText);

    host.setPalette(scheme_with(QColor(27, 27, 27)));
    QEvent change(QEvent::PaletteChange);
    QApplication::sendEvent(&label, &change);

    EXPECT_NE(label.palette().color(QPalette::WindowText), pale)
        << "a red pinned under one scheme sits below the contrast it was written to guarantee under the other";
}

TEST(UiStyle, AButtonStripRunsAcrossAndAButtonColumnDownTheSameWay) {
    (void)atp_ui_tests::ensure_app();
    QWidget host;

    const style::button_bar across = style::make_button_bar(&host);
    const style::button_bar down = style::make_button_column(&host);

    ASSERT_NE(across.row, nullptr);
    ASSERT_NE(down.row, nullptr);
    EXPECT_EQ(across.row->direction(), QBoxLayout::LeftToRight);
    EXPECT_EQ(down.row->direction(), QBoxLayout::TopToBottom);
    EXPECT_EQ(across.row->spacing(), down.row->spacing());
    EXPECT_EQ(across.row->contentsMargins(), down.row->contentsMargins());
}

TEST(UiStyle, AToolButtonWearsEitherAGlyphOrArtworkAndIsFlatEitherWay) {
    (void)atp_ui_tests::ensure_app();
    QWidget host;

    QToolButton* lettered = style::tool_button(style::glyph::add, QStringLiteral("add one"), &host);
    QToolButton* drawn = style::tool_button(icons::soft_wrap(), QStringLiteral("wrap long lines"), &host);

    EXPECT_EQ(lettered->text(), QStringLiteral("+"));
    EXPECT_TRUE(lettered->icon().isNull());
    EXPECT_TRUE(drawn->text().isEmpty());
    EXPECT_FALSE(drawn->icon().isNull());
    EXPECT_TRUE(lettered->autoRaise());
    EXPECT_TRUE(drawn->autoRaise());
}

}  // namespace
