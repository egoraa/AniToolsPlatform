// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_UI_LOG_WIDGET_HPP
#define ATP_STUDIO_UI_LOG_WIDGET_HPP

#include <atp/module/log_level.hpp>

#include <QListWidget>
#include <QPoint>
#include <QString>

namespace atp::studio::ui {

/// The Log dock's list: what report() writes into, newest line first. Unlike the other panels it
/// holds no state of its own and takes no callbacks — it is a view over a log and nothing else. It
/// is a widget rather than a bare QListWidget only because a log one cannot select a stretch of and
/// copy out is of little use when something goes wrong and the message has to reach a bug report.
class log_widget final : public QListWidget {
   public:
    explicit log_widget(QWidget* parent = nullptr);

    /// Puts one line at the top of the log and colours it by severity: an error is the red the
    /// rest of the studio already uses for a refusal, a warning is amber, and anything calmer
    /// keeps the palette's own colour — colouring every line would leave nothing standing out.
    ///
    /// The colour lives here rather than at the call site because it is a property of the view:
    /// what is copied out of the log is the text, which carries the level in words anyway.
    /// @param text the rendered line, stamp and all
    /// @param level severity
    void append(const QString& text, atp::log_level level);

    /// Puts the selected lines on the system clipboard, in the order they are shown. Selecting
    /// nothing copies nothing and leaves whatever is already on the clipboard alone.
    void copy_selection() const;

    /// Text of the selected lines, top to bottom, one per line. selectedItems() answers in
    /// selection order, which is not what someone copying a stretch of a log expects to paste.
    /// @return the joined lines, empty when the selection is
    [[nodiscard]] QString selected_text() const;

   protected:
    void keyPressEvent(QKeyEvent* event) override;

    void contextMenuEvent(QContextMenuEvent* event) override;

   private:
    void show_menu(const QPoint& global);
};

}  // namespace atp::studio::ui

#endif
