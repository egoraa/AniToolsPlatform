// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_UI_LOG_FILTER_HPP
#define ATP_STUDIO_UI_LOG_FILTER_HPP

#include <optional>

#include <QSortFilterProxyModel>
#include <QString>

#include "panels/log_entry.hpp"

namespace atp::studio::ui {

/// What one view of the log shows.
///
/// An empty query is the whole log, an origin alone is "only what modules said" or "only what the
/// studio said", and a path is one instance. That is the whole vocabulary, and it is one type
/// rather than three mechanisms on purpose: a tab is a saved query, so the filter and the tabs
/// cannot drift into two different ideas of what a source is.
struct log_query {
    /// The kind of writer, or nothing for any.
    std::optional<log_origin> origin;

    /// The instance's dotted path, or empty for any.
    QString path;

    [[nodiscard]] bool operator==(const log_query&) const = default;
};

/// One view's share of the log.
///
/// It reads the source through log_model's two roles rather than casting sourceModel() to a
/// log_model*, so it knows nothing about the model beyond them and is tested apart from it.
class log_filter final : public QSortFilterProxyModel {
   public:
    explicit log_filter(QObject* parent = nullptr) : QSortFilterProxyModel(parent) {}

    /// Replaces what this view shows and re-judges what is already there.
    ///
    /// invalidateRowsFilter() and not the beginFilterChange()/endFilterChange() pair, which says the
    /// same thing to the proxy at the price of Qt 6.10 for the whole tree — a floor paid by everyone
    /// building the GUI against a distribution's Qt, for nothing this view asks of it. Qt 6.10
    /// deprecates the call, so the definition silences that one diagnostic where it is made: a NOLINT
    /// reaches clang-tidy alone, and the compiler's own warning is what ATP_WERROR turns into a
    /// failed build.
    /// @param query the new predicate
    void set_query(const log_query& query);

    /// @return what this view shows
    [[nodiscard]] const log_query& query() const {
        return query_;
    }

   protected:
    [[nodiscard]] bool filterAcceptsRow(int row, const QModelIndex& parent) const override;

   private:
    log_query query_;
};

}  // namespace atp::studio::ui

#endif
