// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_UI_LOG_PANEL_HPP
#define ATP_STUDIO_UI_LOG_PANEL_HPP

#include <functional>
#include <utility>
#include <vector>

#include <QPointer>
#include <QString>
#include <QWidget>

#include "panels/log_entry.hpp"
#include "panels/log_filter.hpp"
#include "panels/log_model.hpp"
#include "panels/log_view.hpp"

class QStackedWidget;
class QStyle;
class QTabBar;
class QToolButton;

namespace atp::studio::ui {

/// The Log dock's body: one history, and as many views of it as the reader has asked for.
///
/// A tab is a saved log_query and nothing else — it holds no lines of its own, so clearing,
/// eviction and the list of known sources need no agreement between tabs, there being only one of
/// each. The first tab shows everything and cannot be closed; the rest are opened on demand and
/// live until the window does, which is why none of them reaches the profile.
///
/// The strip of buttons belongs here rather than to the window because one of them opens tabs,
/// which is the panel's business. Its two remembered answers go back out as callbacks, since the
/// profile is the window's: callbacks and not signals because there is no Q_OBJECT in this layer.
class log_panel final : public QWidget {
   public:
    /// @param soft_wrap whether long lines start wrapped, from the profile
    /// @param follow_tail whether a view starts at the end of the log, from the profile
    /// @param parent the dock
    log_panel(bool soft_wrap, bool follow_tail, QWidget* parent = nullptr);

    /// Takes the views down before the history they read.
    ///
    /// The model is a member and the views are children, so without this the order is inverted:
    /// members go before the QWidget base deletes its children, leaving every log_filter pointing
    /// at a destroyed source model for the rest of the teardown. Qt clears the pointer, so nothing
    /// has crashed yet, but a proxy holding a mapping built from a model that is already gone is
    /// not something to leave standing on Qt's good manners.
    ///
    /// It also clears each view's follow-tail callback first, and that is the same kind of refusal
    /// to lean on Qt's manners: emptying a list clamps its scrollbar, so a view can report a change
    /// of state from inside its own destruction, into a handler holding a panel whose views_ has
    /// just been emptied. Nothing fires today only because the callback is a member of log_view and
    /// so dies before the scrollbars of its base — an order nobody promised.
    ~log_panel() override;

    /// Puts one line at the end of the log. Every view judges it by its own query.
    /// @param entry the line
    void append(const log_entry& entry);

    /// Empties the log. The views stay open and every one of them goes empty at once, because the
    /// history they show is one.
    void clear();

    /// Opens a view of one source, or raises the one already showing it.
    ///
    /// Whether a view is already open is decided by the query and never by the title: two tabs may
    /// legitimately be labelled the same and mean different things, while two equal queries always
    /// mean the same lines.
    /// @param query what the view shows
    /// @param title the tab's label
    void open_view(const log_query& query, const QString& title);

    /// @return how many views are open, the view of everything included
    [[nodiscard]] int view_count() const {
        return static_cast<int>(views_.size());
    }

    /// The view being read.
    ///
    /// Nullable, and deliberately so rather than "never null" with an unchecked index behind it:
    /// the one caller that is not a direct answer to a click runs while the panel is being taken
    /// down, where there is no current view and the honest answer is nothing.
    /// @return the current view, or nullptr while the panel is being destroyed
    [[nodiscard]] log_view* current_view() const;

    /// Wraps long lines in every view, the ones opened later included: it is an answer about the
    /// dock, unlike following the tail, which is an answer about the view being read.
    /// @param on whether to wrap
    void set_soft_wrap(bool on);

    /// @return whether long lines are wrapped
    [[nodiscard]] bool soft_wrap() const {
        return soft_wrap_;
    }

    /// Registers what to do when the reader asks for wrapping, so the window can write it to the
    /// profile.
    /// @param handler called with the new state
    void on_soft_wrap_changed(std::function<void(bool)> handler) {
        wrap_changed_ = std::move(handler);
    }

    /// Registers what to do when the reader asks a view to follow the tail — through the button,
    /// which is the only one of the three ways that is a decision.
    ///
    /// Switching tabs moves the button too, and so does scrolling, and neither is worth writing to
    /// the profile: the first is a view coming into sight and the second is a position in a list.
    /// Both therefore reach the button under a QSignalBlocker and never reach here.
    /// @param handler called with the new state
    void on_follow_tail_changed(std::function<void(bool)> handler) {
        follow_changed_ = std::move(handler);
    }

   private:
    /// Builds a view over a filter of its own and adds it as a tab, making it current.
    void add_view(const log_query& query, const QString& title, bool closable);

    /// Takes a view away. Index zero is refused: the view of everything is what the dock is.
    void close_view(int index);

    /// Puts a proxy style on the tab bar, which is what draws its close indicator and says where it
    /// sits. Both are the style's business and neither can be reached from a widget, which is why
    /// two earlier attempts — a `QToolButton` of the panels' own family put in as the tab's button —
    /// answered neither complaint.
    ///
    /// **The cross is artwork of the icons:: family** rather than the style's own, which under a
    /// dark theme is red: the colour the log marks an error in, spent on a way out of a tab. It is
    /// painted straight from `PE_IndicatorTabClose` rather than handed over as `SP_TabCloseButton`,
    /// because QCommonStyle caches that pixmap inside **the application's** style — shared with
    /// every other tab bar in the window, and filled by whichever asked first.
    ///
    /// **And the indicator sits against the edge of its tab.** QCommonStyle places it one
    /// `PM_TabBarTabHSpace`-derived padding in, which is wide enough that the cross reads as
    /// floating in the middle of nothing; `SE_TabBarTabRightButton` is moved so the gap is the
    /// hairline a tab's own frame needs and no more.
    ///
    /// It has to be put on **the indicator as well as the bar**, which is the trap here: QTabBar
    /// draws no cross itself — the indicator is a child widget of its own — and a widget's style is
    /// not inherited from its parent, so a proxy set on the bar alone lays the indicator out and
    /// then leaves the application style to paint it, red and all.
    ///
    /// The proxy is a child of the panel and keeps no base of its own, so it follows the
    /// application style through a change of theme.
    void style_tabs();

    /// Shows the tab bar only once there is a second view. One tab named "All" over a dock whose
    /// whole body is that one list says nothing and costs a row of the text.
    void update_tabs_visible();

    /// Brings the tab now current into sight and points the follow button at its view.
    void show_view(int index);

    /// Drops the menu the open-view button carries: the two kinds of writer, then every instance
    /// the log has heard from.
    void show_open_menu();

    /// Puts the wrap button where the panel stands, without letting that count as the reader's
    /// answer: set_soft_wrap is also the public way in, and a button left behind its own state
    /// answers the next click with a toggle that changes nothing and writes a wrong profile.
    void sync_wrap_button();

    /// Puts the follow button where the current view stands, without letting that count as the
    /// reader's answer — the same reason on_follow_tail_changed gives, one level up.
    ///
    /// The button is held as a QPointer and the view is checked here because this is the one place
    /// a widget of the strip is reached from a callback owned by a sibling — a view's scrollbar
    /// moves while the panel is being torn down. The destructor now severs those callbacks and
    /// takes the views down before the strip, so neither check should fire; both stay because the
    /// alternative is an unchecked pointer whose safety is an order of destruction, and this is the
    /// one path where that order is not the panel's to state.
    void sync_follow_button();

    log_model model_;
    QTabBar* tabs_ = nullptr;
    QStyle* tab_style_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QToolButton* wrap_button_ = nullptr;
    QPointer<QToolButton> follow_button_;
    QToolButton* open_button_ = nullptr;
    std::vector<log_view*> views_;
    bool soft_wrap_ = false;
    bool follow_tail_ = true;
    std::function<void(bool)> wrap_changed_;
    std::function<void(bool)> follow_changed_;
};

}  // namespace atp::studio::ui

#endif
