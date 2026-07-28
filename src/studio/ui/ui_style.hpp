#ifndef ATP_STUDIO_UI_UI_STYLE_HPP
#define ATP_STUDIO_UI_UI_STYLE_HPP

#include <QAbstractItemView>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QString>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

/// Look shared by the editing panels. The panels are assembled from plain widgets rather than
/// styled through one application-wide sheet: a sheet would fight both the native theme and the
/// canvas, which paints itself.
namespace atp::studio::ui::style {

/// Gap between two sections of a panel.
inline constexpr int section_spacing = 12;

/// Gap between the rows inside a section.
inline constexpr int row_spacing = 4;

/// Height an embedded list starts at — about four rows; the rest comes from the layout.
inline constexpr int view_min_height = 90;

/// Frame of a widget holding a value the document refused.
inline constexpr const char* error_frame = "border: 1px solid #c05050;";

/// Glyphs the panels put on their buttons, in one place so that one action looks the same wherever
/// it appears. They are code points rather than literals: the /utf-8 that makes a non-ASCII literal
/// mean what it looks like reaches this target from Qt (qt_enable_utf8_sources), not from our own
/// build, and a glyph should not depend on a flag someone else sets for us.
namespace glyph {
inline constexpr char16_t add = u'+';
inline constexpr char16_t drop = 0x2212;    // MINUS SIGN
inline constexpr char16_t reset = 0x21ba;   // ANTICLOCKWISE OPEN CIRCLE ARROW
inline constexpr char16_t rescan = 0x21bb;  // CLOCKWISE OPEN CIRCLE ARROW
inline constexpr char16_t clear = 0x2715;   // MULTIPLICATION X
}  // namespace glyph

/// A section title: small capitals in the muted text colour, followed by a hairline running to the
/// edge of the panel. It replaces a group box frame — in a narrow dock nested frames cost more
/// width than they buy separation.
/// @param title section name, shown upper-cased
/// @param parent widget the header belongs to
/// @return the header widget, ready to be added to a layout
[[nodiscard]] inline QWidget* section_header(const QString& title, QWidget* parent) {
    auto* head = new QWidget(parent);
    auto* layout = new QHBoxLayout(head);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    auto* label = new QLabel(title.toUpper(), head);
    QFont f = label->font();
    f.setBold(true);
    f.setLetterSpacing(QFont::PercentageSpacing, 105);
    label->setFont(f);
    // The disabled text colour is the theme's own answer to "muted", so the header stays readable
    // on a dark palette without a second set of constants.
    QPalette p = label->palette();
    p.setColor(QPalette::WindowText, p.color(QPalette::Disabled, QPalette::WindowText));
    label->setPalette(p);
    layout->addWidget(label);

    auto* line = new QFrame(head);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line, 1);
    return head;
}

/// A section: the header above a form the rows go into.
struct section {
    QWidget* box = nullptr;       // the whole block — what a caller shows, hides or disables
    QFormLayout* form = nullptr;  // where the rows go
};

/// Builds a section. The form wraps long rows, so a row that does not fit the dock puts its field
/// under the label instead of demanding horizontal scrolling.
/// @param title section name, shown upper-cased
/// @param parent widget the section belongs to
/// @return the block and its form
[[nodiscard]] inline section make_section(const QString& title, QWidget* parent) {
    auto* box = new QWidget(parent);
    auto* layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(row_spacing);
    layout->addWidget(section_header(title, box));

    auto* content = new QWidget(box);
    auto* form = new QFormLayout(content);
    form->setContentsMargins(0, 0, 0, 0);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setLabelAlignment(Qt::AlignLeft);
    form->setVerticalSpacing(row_spacing);
    layout->addWidget(content);
    return {box, form};
}

/// A small glyph button — the panels' only button shape, so that add, drop and reset read as one
/// family wherever they appear.
/// @param g one of the glyph constants
/// @param tip what the button does
/// @param parent widget the button belongs to
[[nodiscard]] inline QToolButton* tool_button(char16_t g, const QString& tip, QWidget* parent) {
    auto* button = new QToolButton(parent);
    button->setText(QString(QChar(g)));
    button->setToolTip(tip);
    button->setAutoRaise(true);
    return button;
}

/// A strip of glyph buttons under a view — where a panel keeps the actions on what the view shows.
struct button_bar {
    QWidget* box = nullptr;      // the strip itself
    QHBoxLayout* row = nullptr;  // where the buttons go; end with addStretch(1) to keep them left
};

/// Builds an empty button strip.
/// @param parent widget the strip belongs to
/// @return the strip and its layout
[[nodiscard]] inline button_bar make_button_bar(QWidget* parent) {
    auto* box = new QWidget(parent);
    auto* row = new QHBoxLayout(box);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(2);
    return {box, row};
}

/// Gives an embedded list or table the look of part of the panel rather than of a window of its
/// own: no frame of its own — the dock already draws one — and the height to grow into.
/// @param view the list or table
inline void embed_view(QAbstractItemView* view) {
    view->setFrameShape(QFrame::NoFrame);
    view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    view->setMinimumHeight(view_min_height);
}

/// Marks a widget as holding a rejected value: the reason belongs on the widget, not only in the
/// log dock in the far corner of the window.
/// @param w the editor
/// @param reason what the document said
inline void mark_error(QWidget* w, const QString& reason) {
    w->setStyleSheet(error_frame);
    w->setToolTip(reason);
}

/// Takes the rejection mark off.
/// @param w the editor
inline void clear_error(QWidget* w) {
    w->setStyleSheet(QString());
    w->setToolTip(QString());
}

}  // namespace atp::studio::ui::style

#endif  // ATP_STUDIO_UI_UI_STYLE_HPP
