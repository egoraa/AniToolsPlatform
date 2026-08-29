// SPDX-License-Identifier: Apache-2.0
#include "panels/log_view.hpp"

#include "kit/ui_style.hpp"
#include "panels/log_model.hpp"

#include <QAbstractItemModel>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMenu>
#include <QModelIndex>
#include <QScrollBar>
#include <QStringList>

namespace atp::studio::ui {

log_view::log_view(QWidget* parent) : QListView(parent) {
    style::embed_view(this);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setToolTip("Ctrl+A selects the log, Ctrl+C copies the selection");
    setTextElideMode(Qt::ElideNone);
    setWordWrap(soft_wrap_);
    setResizeMode(QListView::Adjust);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    QObject::connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { note_follow_tail(at_end()); });
    QObject::connect(verticalScrollBar(), &QScrollBar::rangeChanged, this, [this] {
        if (follow_tail_) {
            scrollToBottom();
        }
    });
}

void log_view::setModel(QAbstractItemModel* source) {
    QListView::setModel(source);
    if (follow_tail_) {
        scrollToBottom();
    }
}

int log_view::line_count() const {
    return model() != nullptr ? model()->rowCount() : 0;
}

QString log_view::selected_text() const {
    const QAbstractItemModel* source = model();
    if (source == nullptr || selectionModel() == nullptr) {
        return {};
    }
    QStringList lines;
    for (int row = 0; row < source->rowCount(); ++row) {
        const QModelIndex at = source->index(row, 0);
        if (selectionModel()->isSelected(at)) {
            lines << at.data(Qt::DisplayRole).toString();
        }
    }
    return lines.join(QLatin1Char('\n'));
}

void log_view::copy_selection() const {
    const QString text = selected_text();
    if (text.isEmpty()) {
        return;
    }
    QApplication::clipboard()->setText(text);
}

void log_view::set_soft_wrap(bool on) {
    if (soft_wrap_ == on) {
        return;
    }
    soft_wrap_ = on;
    setWordWrap(on);
    setHorizontalScrollBarPolicy(on ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
    if (follow_tail_) {
        scrollToBottom();
    }
}

void log_view::set_follow_tail(bool on) {
    note_follow_tail(on);
    if (on) {
        scrollToBottom();
    }
}

void log_view::note_follow_tail(bool on) {
    if (follow_tail_ == on) {
        return;
    }
    follow_tail_ = on;
    if (follow_changed_) {
        follow_changed_(on);
    }
}

bool log_view::at_end() const {
    const QScrollBar* bar = verticalScrollBar();
    return bar->value() >= bar->maximum();
}

void log_view::keyPressEvent(QKeyEvent* event) {
    if (event->matches(QKeySequence::Copy)) {
        copy_selection();
        event->accept();
        return;
    }
    QListView::keyPressEvent(event);
}

void log_view::contextMenuEvent(QContextMenuEvent* event) {
    show_menu(event->globalPos());
    event->accept();
}

void log_view::show_menu(const QPoint& global) {
    QMenu menu;
    QAction* copy = menu.addAction(QStringLiteral("Copy"));
    copy->setShortcut(QKeySequence::Copy);
    copy->setEnabled(selectionModel() != nullptr && selectionModel()->hasSelection());
    QAction* select_all = menu.addAction(QStringLiteral("Select All"));
    select_all->setShortcut(QKeySequence::SelectAll);
    select_all->setEnabled(line_count() > 0);

    const QString path = indexAt(viewport()->mapFromGlobal(global)).data(log_model::path_role).toString();
    QAction* open_view = nullptr;
    if (!path.isEmpty() && open_view_) {
        menu.addSeparator();
        open_view = menu.addAction(QStringLiteral("Open a tab for %1").arg(path));
    }

    QAction* clear_log = nullptr;
    if (clear_log_) {
        menu.addSeparator();
        clear_log = menu.addAction(QStringLiteral("Clear"));
    }

    QAction* chosen = menu.exec(global);
    if (chosen == nullptr) {
        return;
    }
    if (chosen == copy) {
        copy_selection();
    } else if (chosen == select_all) {
        selectAll();
    } else if (chosen == open_view) {
        open_view_(path);
    } else if (chosen == clear_log) {
        clear_log_();
    }
}

}  // namespace atp::studio::ui
