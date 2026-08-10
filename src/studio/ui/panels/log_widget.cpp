// SPDX-License-Identifier: Apache-2.0
#include "panels/log_widget.hpp"

#include "kit/ui_style.hpp"

#include <QAction>
#include <QApplication>
#include <QBrush>
#include <QClipboard>
#include <QColor>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QListWidgetItem>
#include <QMenu>
#include <QStringList>

namespace atp::studio::ui {

log_widget::log_widget(QWidget* parent) : QListWidget(parent) {
    style::embed_view(this);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setToolTip("Ctrl+A selects the log, Ctrl+C copies the selection");
}

void log_widget::append(const QString& text, atp::log_level level) {
    auto* line = new QListWidgetItem(text);
    if (level == atp::log_level::error) {
        line->setForeground(QBrush(QColor(220, 80, 80)));
    } else if (level == atp::log_level::warning) {
        line->setForeground(QBrush(QColor(220, 170, 80)));
    }
    insertItem(0, line);
}

QString log_widget::selected_text() const {
    QStringList lines;
    for (int i = 0; i < count(); ++i) {
        const QListWidgetItem* line = item(i);
        if (line != nullptr && line->isSelected()) {
            lines << line->text();
        }
    }
    return lines.join(QLatin1Char('\n'));
}

void log_widget::copy_selection() const {
    const QString text = selected_text();
    if (text.isEmpty()) {
        return;
    }
    QApplication::clipboard()->setText(text);
}

void log_widget::keyPressEvent(QKeyEvent* event) {
    if (event->matches(QKeySequence::Copy)) {
        copy_selection();
        event->accept();
        return;
    }
    QListWidget::keyPressEvent(event);
}

void log_widget::contextMenuEvent(QContextMenuEvent* event) {
    show_menu(event->globalPos());
    event->accept();
}

void log_widget::show_menu(const QPoint& global) {
    QMenu menu;
    QAction* copy = menu.addAction(QStringLiteral("Copy"));
    copy->setShortcut(QKeySequence::Copy);
    copy->setEnabled(!selectedItems().isEmpty());
    QAction* select_all = menu.addAction(QStringLiteral("Select All"));
    select_all->setShortcut(QKeySequence::SelectAll);
    select_all->setEnabled(count() > 0);
    menu.addSeparator();
    QAction* clear_log = menu.addAction(QStringLiteral("Clear"));
    clear_log->setEnabled(count() > 0);

    QAction* chosen = menu.exec(global);
    if (chosen == copy) {
        copy_selection();
    } else if (chosen == select_all) {
        selectAll();
    } else if (chosen == clear_log) {
        clear();
    }
}

}  // namespace atp::studio::ui
