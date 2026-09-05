// SPDX-License-Identifier: Apache-2.0
#include "panels/log_panel.hpp"

#include "kit/icons.hpp"
#include "kit/ui_style.hpp"

#include <algorithm>
#include <cstddef>

#include <QAction>
#include <QColor>
#include <QHBoxLayout>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPoint>
#include <QProxyStyle>
#include <QRect>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStringList>
#include <QStyle>
#include <QStyleOptionTab>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace atp::studio::ui {
namespace {

constexpr int close_button_margin = 2;
constexpr double close_glyph_share = 0.6;
constexpr float hover_wash_alpha = 0.25F;
constexpr int hover_wash_radius = 2;

class tab_close_style final : public QProxyStyle {
   public:
    void drawPrimitive(PrimitiveElement element,
                       const QStyleOption* option,
                       QPainter* painter,
                       const QWidget* widget) const override {
        if (element != PE_IndicatorTabClose) {
            QProxyStyle::drawPrimitive(element, option, painter, widget);
            return;
        }
        painter->setRenderHint(QPainter::Antialiasing, true);
        if ((option->state & State_MouseOver) != 0) {
            QColor wash = option->palette.color(QPalette::Highlight);
            wash.setAlphaF(hover_wash_alpha);
            painter->setPen(Qt::NoPen);
            painter->setBrush(wash);
            painter->drawRoundedRect(option->rect, hover_wash_radius, hover_wash_radius);
        }
        const int side =
            std::max(1, qRound(close_glyph_share * proxy()->pixelMetric(PM_SmallIconSize, option, widget)));
        QRect at(0, 0, side, side);
        at.moveCenter(option->rect.center());
        glyph_.paint(painter, at, Qt::AlignCenter,
                     (option->state & State_Enabled) != 0 ? QIcon::Normal : QIcon::Disabled);
    }

    [[nodiscard]] QRect subElementRect(SubElement element,
                                       const QStyleOption* option,
                                       const QWidget* widget) const override {
        QRect rect = QProxyStyle::subElementRect(element, option, widget);
        if (element == SE_TabBarTabRightButton) {
            if (const auto* tab = qstyleoption_cast<const QStyleOptionTab*>(option); tab != nullptr) {
                rect.moveRight(tab->rect.right() - close_button_margin);
            }
        }
        return rect;
    }

   private:
    QIcon glyph_ = icons::close_tab();
};

}  // namespace

log_panel::log_panel(bool soft_wrap, bool follow_tail, QWidget* parent)
    : QWidget(parent), soft_wrap_(soft_wrap), follow_tail_(follow_tail) {
    auto* layout = new QHBoxLayout(this);
    layout->setSpacing(style::row_spacing);

    const style::button_bar bar = style::make_button_column(this);
    wrap_button_ = style::tool_button(icons::soft_wrap(), "wrap long lines", bar.box);
    wrap_button_->setObjectName(QStringLiteral("log.soft_wrap"));
    wrap_button_->setCheckable(true);
    wrap_button_->setChecked(soft_wrap);
    follow_button_ = style::tool_button(icons::scroll_to_end(), "follow the end of the log", bar.box);
    follow_button_->setObjectName(QStringLiteral("log.follow_tail"));
    follow_button_->setCheckable(true);
    follow_button_->setChecked(follow_tail);
    open_button_ = style::tool_button(icons::open_view(), "open a tab for one source", bar.box);
    open_button_->setObjectName(QStringLiteral("log.open_view"));
    auto* clear_button = style::tool_button(icons::clear_all(), "clear the log", bar.box);
    clear_button->setObjectName(QStringLiteral("log.clear"));
    bar.row->addWidget(wrap_button_);
    bar.row->addWidget(follow_button_);
    bar.row->addWidget(open_button_);
    bar.row->addWidget(clear_button);
    bar.row->addStretch(1);
    layout->addWidget(bar.box);

    auto* body = new QWidget(this);
    auto* column = new QVBoxLayout(body);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    tabs_ = new QTabBar(body);
    tabs_->setObjectName(QStringLiteral("log.tabs"));
    tabs_->setTabsClosable(true);
    tabs_->setExpanding(false);
    tabs_->setDrawBase(false);
    style_tabs();
    stack_ = new QStackedWidget(body);
    column->addWidget(tabs_);
    column->addWidget(stack_, 1);
    layout->addWidget(body, 1);

    add_view(log_query{}, QStringLiteral("All"), false);
    views_.front()->setObjectName(QStringLiteral("log.lines"));

    QObject::connect(tabs_, &QTabBar::currentChanged, this, [this](int index) { show_view(index); });
    QObject::connect(tabs_, &QTabBar::tabCloseRequested, this, [this](int index) { close_view(index); });
    QObject::connect(wrap_button_, &QToolButton::toggled, this, [this](bool on) {
        set_soft_wrap(on);
        if (wrap_changed_) {
            wrap_changed_(on);
        }
    });
    QObject::connect(follow_button_, &QToolButton::toggled, this, [this](bool on) {
        follow_tail_ = on;
        if (log_view* view = current_view(); view != nullptr) {
            view->set_follow_tail(on);
        }
        if (follow_changed_) {
            follow_changed_(on);
        }
    });
    QObject::connect(open_button_, &QToolButton::clicked, this, [this] { show_open_menu(); });
    QObject::connect(clear_button, &QToolButton::clicked, this, [this] { clear(); });
}

log_panel::~log_panel() {
    for (log_view* view : views_) {
        view->on_follow_tail_changed({});
    }
    views_.clear();
    delete stack_;
    stack_ = nullptr;
}

void log_panel::append(const log_entry& entry) {
    model_.append(entry);
}

void log_panel::clear() {
    model_.clear();
}

log_view* log_panel::current_view() const {
    const int index = stack_ != nullptr ? stack_->currentIndex() : -1;
    if (index < 0 || index >= view_count()) {
        return nullptr;
    }
    return views_[static_cast<std::size_t>(index)];
}

void log_panel::open_view(const log_query& query, const QString& title) {
    for (std::size_t i = 0; i < views_.size(); ++i) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
        if (static_cast<log_filter*>(views_[i]->model())->query() == query) {
            tabs_->setCurrentIndex(static_cast<int>(i));
            return;
        }
    }
    add_view(query, title, true);
}

void log_panel::add_view(const log_query& query, const QString& title, bool closable) {
    auto* view = new log_view(stack_);
    auto* filter = new log_filter(view);
    filter->setSourceModel(&model_);
    filter->set_query(query);
    view->setModel(filter);
    view->set_soft_wrap(soft_wrap_);
    view->set_follow_tail(follow_tail_);
    view->on_follow_tail_changed([this, view](bool) {
        if (view == current_view()) {
            sync_follow_button();
        }
    });
    view->on_open_view_requested([this](const QString& path) { open_view({std::nullopt, path}, path); });
    view->on_clear_requested([this] { clear(); });

    views_.push_back(view);
    stack_->addWidget(view);
    const int index = tabs_->addTab(title);
    if (!closable) {
        tabs_->setTabButton(index, QTabBar::RightSide, nullptr);
    } else if (QWidget* indicator = tabs_->tabButton(index, QTabBar::RightSide); indicator != nullptr) {
        indicator->setStyle(tab_style_);
    }
    tabs_->setCurrentIndex(index);
    update_tabs_visible();
}

void log_panel::style_tabs() {
    auto* proxy = new tab_close_style();
    proxy->setParent(this);
    tab_style_ = proxy;
    tabs_->setStyle(tab_style_);
}

void log_panel::update_tabs_visible() {
    tabs_->setVisible(view_count() > 1);
}

void log_panel::close_view(int index) {
    if (index <= 0 || index >= view_count()) {
        return;
    }
    log_view* view = views_[static_cast<std::size_t>(index)];
    views_.erase(views_.begin() + index);
    stack_->removeWidget(view);
    tabs_->removeTab(index);
    view->deleteLater();
    show_view(tabs_->currentIndex());
    update_tabs_visible();
}

void log_panel::show_view(int index) {
    if (index < 0 || index >= view_count()) {
        return;
    }
    stack_->setCurrentIndex(index);
    sync_follow_button();
}

void log_panel::sync_follow_button() {
    const log_view* view = current_view();
    if (follow_button_.isNull() || view == nullptr) {
        return;
    }
    const QSignalBlocker quiet(follow_button_);
    follow_button_->setChecked(view->follow_tail());
}

void log_panel::set_soft_wrap(bool on) {
    soft_wrap_ = on;
    for (log_view* view : views_) {
        view->set_soft_wrap(on);
    }
    sync_wrap_button();
}

void log_panel::sync_wrap_button() {
    if (wrap_button_ == nullptr) {
        return;
    }
    const QSignalBlocker quiet(wrap_button_);
    wrap_button_->setChecked(soft_wrap_);
}

void log_panel::show_open_menu() {
    QMenu menu;
    QAction* modules = menu.addAction(QStringLiteral("Modules only"));
    QAction* system = menu.addAction(QStringLiteral("System only"));
    std::vector<std::pair<QAction*, QString>> instances;
    const QStringList paths = model_.paths();
    if (!paths.isEmpty()) {
        menu.addSeparator();
        for (const QString& path : paths) {
            instances.emplace_back(menu.addAction(path), path);
        }
    }

    QAction* chosen = menu.exec(open_button_->mapToGlobal(QPoint(0, open_button_->height())));
    if (chosen == nullptr) {
        return;
    }
    if (chosen == modules) {
        open_view({log_origin::module, QString()}, QStringLiteral("Modules"));
        return;
    }
    if (chosen == system) {
        open_view({log_origin::system, QString()}, QStringLiteral("System"));
        return;
    }
    for (const auto& [action, path] : instances) {
        if (chosen == action) {
            open_view({std::nullopt, path}, path);
            return;
        }
    }
}

}  // namespace atp::studio::ui
