// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_UI_UI_STYLE_HPP
#define ATP_STUDIO_UI_UI_STYLE_HPP

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QBoxLayout>
#include <QColor>
#include <QEvent>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QObject>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QString>
#include <QStyle>
#include <QToolButton>
#include <QTreeView>
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

/// Width of the frame drawn round a rejected editor. Two pixels rather than one: at one it competes
/// with the editor's own border and is read as part of it.
inline constexpr int error_frame_width = 2;

/// Height an embedded list starts at — about four rows; the rest comes from the layout.
inline constexpr int view_min_height = 90;

/// Step a tree indents a level by: the metric the style builds its indicators on — the size of a
/// checkbox. Narrower than the platform indentation, which is meant for windows rather than for a
/// dock — here every level is width taken away from the names — and, unlike a number picked once, it
/// keeps pace with the screen density and with whatever theme the widget ends up under.
/// @param w the tree; the metric is asked of its style, not of the application's
/// @return the indentation, in logical pixels
[[nodiscard]] inline int tree_indent(const QWidget* w) {
    return w->style()->pixelMetric(QStyle::PM_IndicatorWidth, nullptr, w);
}

/// Colour of everything that says something was refused — the tint mark_error puts on an editor and
/// the text error_text paints beside it. One function, because the two appear together and a reason
/// drawn in a different red than the field it explains reads as a second, unrelated fault.
///
/// Derived from the palette rather than fixed. A single red cannot be readable on both schemes: the
/// constant this replaced sat below three to one against a dark ground, and text owes four and a
/// half. The rule is the one canvas_palette::node_alert follows and the numbers are the same; the
/// two are separate because the grounds are — that red is judged against a node body, this one
/// against the panel behind it.
/// @param p palette of the widget the mark will appear on
/// @return the red to use on that scheme
[[nodiscard]] inline QColor error_ink(const QPalette& p) {
    const bool dark = p.color(QPalette::Base).lightness() < 128;
    return QColor::fromHsl(0, 150, dark ? 187 : 95);
}

/// Glyphs the panels put on their buttons, in one place so that one action looks the same wherever
/// it appears. They are code points rather than literals: the /utf-8 that makes a non-ASCII literal
/// mean what it looks like reaches this target from Qt (qt_enable_utf8_sources), not from our own
/// build, and a glyph should not depend on a flag someone else sets for us.
namespace glyph {
inline constexpr char16_t add = u'+';
inline constexpr char16_t drop = 0x2212;
inline constexpr char16_t reset = 0x21ba;
inline constexpr char16_t rescan = 0x21bb;
}  // namespace glyph

/// Paints a label in the palette's placeholder colour: for text that is there to be found when
/// looked for rather than read on the way past — a section title, a timestamp beside a control, the
/// sentence over an empty view.
///
/// A **role** and not a colour. Copying the resolved colour into the label's own palette — which is
/// what this did — pins that role, so the label keeps the grey of whichever scheme was current when
/// it was built and stops following the theme; on a light-to-dark switch it then sits at the wrong
/// end of the contrast the rest of the canvas work is careful about. Naming the role instead leaves
/// the resolving to Qt, which redoes it on every palette change.
/// @param label the label to mute
inline void muted(QLabel* label) {
    label->setForegroundRole(QPalette::PlaceholderText);
}

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
    muted(label);
    layout->addWidget(label);

    auto* line = new QFrame(head);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line, 1);
    return head;
}

/// A section: the header above a form the rows go into.
struct section {
    QWidget* box = nullptr;
    QFormLayout* form = nullptr;
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

/// A small glyph button — the panels' usual button shape, so that add, drop and reset read as one
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

/// The same button wearing artwork instead of a letter — for a strip whose actions no glyph says.
/// See icons::soft_wrap for when that is the case and why the whole strip then changes together.
/// @param artwork one of the icons:: family
/// @param tip what the button does
/// @param parent widget the button belongs to
[[nodiscard]] inline QToolButton* tool_button(const QIcon& artwork, const QString& tip, QWidget* parent) {
    auto* button = new QToolButton(parent);
    button->setIcon(artwork);
    button->setToolTip(tip);
    button->setAutoRaise(true);
    return button;
}

/// A strip of buttons along a view — where a panel keeps the actions on what the view shows. Under
/// it when the panel has the height to spare, beside it when the view is the panel.
struct button_bar {
    QWidget* box = nullptr;
    QBoxLayout* row = nullptr;
};

/// Builds an empty button strip, running across under a view.
/// @param parent widget the strip belongs to
/// @return the strip and its layout
[[nodiscard]] inline button_bar make_button_bar(QWidget* parent) {
    auto* box = new QWidget(parent);
    auto* row = new QHBoxLayout(box);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(2);
    return {box, row};
}

/// The same strip stood on end, to run down beside a view rather than across under it. A dock whose
/// whole body is one view has no row to spare — a strip under it is a strip taken off the text —
/// while a column costs width the text was not using anyway.
/// @param parent widget the strip belongs to
/// @return the strip and its layout
[[nodiscard]] inline button_bar make_button_column(QWidget* parent) {
    auto* box = new QWidget(parent);
    auto* column = new QVBoxLayout(box);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(2);
    return {box, column};
}

/// Puts a sentence over a view that has no rows, and takes it away as soon as it has one.
///
/// An empty table drawn as a row of headers over a blank slab reads as broken; a line saying what
/// would fill it reads as waiting. The sentence is a **label over the viewport** and not a row of
/// the model, which was the first idea and is wrong twice over: `UiRuntimeWidget` pins "an empty
/// table has no rows" as a contract, and a placeholder row would also join the selection, the
/// copying and the sorting as if it were data. Qt offers no placeholder of its own above
/// QLineEdit and QComboBox, so the label, its centring and its visibility are written out here.
///
/// The label is found by `viewport()->findChild<QLabel*>("view.placeholder")`, which is how a test
/// asks what it says. The layout is given SetNoConstraint because a layout otherwise sets a minimum
/// size on the widget it manages — and that widget is the scroll area's own viewport, whose size the
/// scroll area is supposed to decide: with the default constraint a narrow dock would clip its table
/// instead of scrolling it.
/// @param view the list or table, already carrying the model it will keep — the visibility is wired
///        to that model's signals, so a view whose model is replaced later would freeze the note in
///        whatever state it was last in
/// @param text what to show while the view has no rows
inline void set_placeholder(QAbstractItemView* view, const QString& text) {
    QAbstractItemModel* model = view->model();
    if (model == nullptr || view->viewport()->layout() != nullptr) {
        return;
    }
    auto* label = new QLabel(text, view->viewport());
    label->setObjectName(QLatin1String("view.placeholder"));
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    muted(label);

    auto* layout = new QVBoxLayout(view->viewport());
    layout->setContentsMargins(section_spacing, section_spacing, section_spacing, section_spacing);
    layout->setSizeConstraint(QLayout::SetNoConstraint);
    layout->addWidget(label);

    const auto sync = [view, label] { label->setVisible(view->model() != nullptr && view->model()->rowCount() == 0); };
    QObject::connect(model, &QAbstractItemModel::modelReset, label, sync);
    QObject::connect(model, &QAbstractItemModel::rowsInserted, label, sync);
    QObject::connect(model, &QAbstractItemModel::rowsRemoved, label, sync);
    sync();
}

/// Gives an embedded list or table the look of part of the panel rather than of a window of its
/// own: no frame of its own — the dock already draws one — and the height to grow into.
/// @param view the list or table
inline void embed_view(QAbstractItemView* view) {
    view->setFrameShape(QFrame::NoFrame);
    view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    view->setMinimumHeight(view_min_height);
}

namespace detail {

/// Puts a tree's indentation back after its style changes. The metric belongs to the style, and a
/// theme swap replaces the style under a widget that is already built, so a value set once would
/// stay at the size the previous theme asked for. A filter rather than an overridden changeEvent,
/// because not every tree has a class of its own to override it in — some are held as plain members.
class indent_keeper final : public QObject {
   public:
    explicit indent_keeper(QTreeView* view) : QObject(view), view_(view) {}

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::StyleChange) {
            view_->setIndentation(tree_indent(view_));
        }
        return QObject::eventFilter(watched, event);
    }

   private:
    QTreeView* view_;
};

}  // namespace detail

/// Gives an embedded tree the look of part of the panel — as embed_view does — and the panel's
/// indentation, kept in step with the style from here on.
/// @param view the tree
inline void embed_tree(QTreeView* view) {
    embed_view(view);
    view->setIndentation(tree_indent(view));
    view->installEventFilter(new detail::indent_keeper(view));
}

namespace detail {

/// The frame mark_error draws over a rejected editor.
///
/// A child widget and not a palette tint, and not a style sheet either — three mechanisms were tried
/// and only this one is all of: **idempotent**, since marking twice leaves one frame where mixing a
/// tint into an already tinted palette walked the field towards solid red; **theme-following**, since
/// the colour is read at paint time rather than pinned into the widget's own palette, where it would
/// keep the scheme it was set under; and **visible on every editor**, since a non-editable combo box
/// and a check box paint from Button rather than Base and a tint of Base left them unmarked. A style
/// sheet was the original mechanism and switches a line edit off its native rendering entirely.
///
/// Transparent to the mouse, so the editor underneath goes on being edited, and re-sized with it.
class error_frame final : public QWidget {
   public:
    static constexpr const char* name = "error.frame";

    explicit error_frame(QWidget* parent) : QWidget(parent) {
        setObjectName(QLatin1String(name));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setGeometry(parent->rect());
        parent->installEventFilter(this);
        raise();
        show();
    }

   protected:
    void paintEvent(QPaintEvent* event) override {
        QWidget::paintEvent(event);
        QPainter painter(this);
        painter.setPen(QPen(error_ink(palette()), error_frame_width));
        const int inset = error_frame_width / 2;
        painter.drawRect(rect().adjusted(inset, inset, -inset - 1, -inset - 1));
    }

    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::Resize && watched == parentWidget()) {
            setGeometry(parentWidget()->rect());
        }
        return QWidget::eventFilter(watched, event);
    }
};

/// Puts a label's refusal colour back after the scheme under it changes. The colour is a value and
/// not a role — QPalette has none for "refused" — so, unlike muted(), it cannot be left to Qt to
/// resolve and has to be re-read here instead.
class error_ink_keeper final : public QObject {
   public:
    static constexpr const char* name = "error.ink";

    explicit error_ink_keeper(QLabel* label) : QObject(label), label_(label) {
        setObjectName(QLatin1String(name));
    }

    void apply() {
        QPalette p;
        p.setColor(QPalette::WindowText, error_ink(label_->palette()));
        label_->setPalette(p);
    }

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange) {
            apply();
        }
        return QObject::eventFilter(watched, event);
    }

   private:
    QLabel* label_;
};

}  // namespace detail

/// Marks a widget as holding a rejected value: the reason belongs on the widget, not only in the log
/// dock in the far corner of the window.
///
/// Marking an already marked widget only replaces the reason — see detail::error_frame for why the
/// mark is a frame drawn over the editor rather than a colour written into it.
/// @param w the editor
/// @param reason what the project said
inline void mark_error(QWidget* w, const QString& reason) {
    if (w->findChild<QWidget*>(QLatin1String(detail::error_frame::name), Qt::FindDirectChildrenOnly) == nullptr) {
        new detail::error_frame(w);
    }
    w->setToolTip(reason);
}

/// Paints a label in the colour of a refusal: for a reason that has to be read on the way past rather
/// than found by hovering. A tooltip is enough for a row that went back to its old value; it is not
/// enough for a form that is not there at all, where the reason is the only thing telling the reader
/// what to do next.
///
/// Only WindowText is pinned, and it is re-read on every scheme change: writing back the label's own
/// resolved palette would pin every role at once and leave the label in the scheme it was marked
/// under, which is the trap muted() was rewritten to get out of.
/// @param label the label to paint
inline void error_text(QLabel* label) {
    auto* found = label->findChild<QObject*>(QLatin1String(detail::error_ink_keeper::name), Qt::FindDirectChildrenOnly);
    auto* keeper = dynamic_cast<detail::error_ink_keeper*>(found);
    if (keeper == nullptr) {
        keeper = new detail::error_ink_keeper(label);
        label->installEventFilter(keeper);
    }
    keeper->apply();
}

/// Takes the rejection mark off. Harmless on a widget that never carried one.
/// @param w the editor
inline void clear_error(QWidget* w) {
    for (QWidget* frame :
         w->findChildren<QWidget*>(QLatin1String(detail::error_frame::name), Qt::FindDirectChildrenOnly)) {
        delete frame;
    }
    w->setToolTip(QString());
}

}  // namespace atp::studio::ui::style

#endif
