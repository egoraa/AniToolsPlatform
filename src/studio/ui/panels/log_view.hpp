// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_UI_LOG_VIEW_HPP
#define ATP_STUDIO_UI_LOG_VIEW_HPP

#include <functional>
#include <utility>

#include <QListView>
#include <QPoint>
#include <QString>

namespace atp::studio::ui {

/// One view of the log: oldest line first, the way a console reads.
///
/// It holds no lines — those are in log_model, once, behind every view of them — but it does hold
/// how a log is drawn and read: whether long lines wrap, whether the view stays at the end as it
/// grows, and what a stretch of it copies out as. All three belong here rather than to the dock,
/// because all three are answers about this view and nothing else can give them; with tabs there is
/// more than one view, and following the tail is a property of the one being read.
///
/// It is a class of its own rather than a bare QListView for a second reason as well: a log one
/// cannot select a stretch of and copy out is of little use when something goes wrong and the
/// message has to reach a bug report.
class log_view final : public QListView {
   public:
    explicit log_view(QWidget* parent = nullptr);

    /// Takes the model and puts a following view at the end of it at once.
    ///
    /// Following itself is driven from the scrollbar's range rather than from row insertion, and
    /// that is the answer to eviction rather than a detail of it: dropping the oldest lines moves
    /// the scrollbar, the valueChanged handler reads any movement as "the reader scrolled", and a
    /// talkative pipeline would unstick itself from the tail the moment the log reached its
    /// ceiling. An insertion and an eviction both change the range, so a following view is taken to
    /// the end from there, before the shift can be read as a decision.
    /// @param source the model to show, normally a log_filter
    void setModel(QAbstractItemModel* source) override;

    /// Puts the selected lines on the system clipboard, in the order they are shown. Selecting
    /// nothing copies nothing and leaves whatever is already on the clipboard alone.
    void copy_selection() const;

    /// Text of the selected lines, top to bottom, one per line. selectedIndexes() answers in
    /// selection order, which is not what someone copying a stretch of a log expects to paste, so
    /// this walks the rows rather than sorting what the selection hands over.
    /// @return the joined lines, empty when the selection is
    [[nodiscard]] QString selected_text() const;

    /// Wraps a line too long for the dock onto the next row instead of leaving it to be reached by
    /// scrolling sideways. Off, the line is neither wrapped nor elided: an elided message is one
    /// nobody can copy whole, and a horizontal scrollbar at least keeps the text.
    ///
    /// Asking for the state it is already in does nothing, which is why the constructor writes the
    /// initial state through to the view rather than trusting it to be Qt's default: were the two
    /// ever to disagree, the first call would be the one that is dropped.
    /// @param on whether to wrap
    void set_soft_wrap(bool on);

    /// @return whether long lines are wrapped
    [[nodiscard]] bool soft_wrap() const {
        return soft_wrap_;
    }

    /// Keeps the view at the end of the log as lines arrive, and takes it there now.
    ///
    /// It is deliberately not sticky against the reader: scrolling away from the end turns it off by
    /// itself, and scrolling back to the end turns it on again. Without that, following the tail
    /// would yank the text out from under anyone trying to read what has already been written, which
    /// is the one thing a log is for.
    /// @param on whether to follow
    void set_follow_tail(bool on);

    /// @return whether the view is following the end of the log
    [[nodiscard]] bool follow_tail() const {
        return follow_tail_;
    }

    /// Registers what to do when following turns itself on or off — how the button that commands it
    /// learns that the reader has just overruled it by scrolling. A callback and not a signal
    /// because there is no Q_OBJECT anywhere in this layer.
    ///
    /// It carries none of the lifetime a Qt connection with a context object would: the state can
    /// change while the view is being torn down, since emptying a list clamps its scrollbar, and a
    /// handler holding a sibling widget it was built beside is holding something that may already be
    /// gone — siblings die in the order they were added, not in the order that would suit this. A
    /// handler that reaches for one owes a QPointer.
    /// @param handler called with the new state, on this thread, after it has changed
    void on_follow_tail_changed(std::function<void(bool)> handler) {
        follow_changed_ = std::move(handler);
    }

    /// Registers what to do when the reader asks, from the context menu, for a tab of its own for
    /// the instance whose line they clicked. The menu offers that only on a module's line, so the
    /// handler is never called with an empty path.
    /// @param handler called with the instance's dotted path
    void on_open_view_requested(std::function<void(const QString&)> handler) {
        open_view_ = std::move(handler);
    }

    /// Registers what to do when the reader clears the log from the context menu.
    ///
    /// The view cannot do it itself, and that is the point rather than an inconvenience: the lines
    /// belong to the model every view shares, so clearing is an act on the log and not on this
    /// window onto it. Without a handler the menu offers nothing to clear.
    ///
    /// For the same reason the entry is never greyed out by this view's row count, which counts
    /// only what its query lets through: a tab opened for a source that has said nothing yet would
    /// offer a dead "Clear" beside a live clear button in the strip, both of them acting on the one
    /// history.
    /// @param handler called with no argument
    void on_clear_requested(std::function<void()> handler) {
        clear_log_ = std::move(handler);
    }

   protected:
    void keyPressEvent(QKeyEvent* event) override;

    void contextMenuEvent(QContextMenuEvent* event) override;

   private:
    void show_menu(const QPoint& global);

    /// Records the new state and tells whoever asked, without moving the view. The half of
    /// set_follow_tail that the scrollbar may run: taking the view to the end from inside the
    /// handler that watches the view move is a loop.
    void note_follow_tail(bool on);

    /// Whether the vertical scrollbar is as far down as it goes — an empty log included, which is
    /// why this is not a comparison against a line count.
    [[nodiscard]] bool at_end() const;

    /// How many lines the model holds, zero when there is no model.
    [[nodiscard]] int line_count() const;

    bool soft_wrap_ = false;
    bool follow_tail_ = true;
    std::function<void(bool)> follow_changed_;
    std::function<void(const QString&)> open_view_;
    std::function<void()> clear_log_;
};

}  // namespace atp::studio::ui

#endif
