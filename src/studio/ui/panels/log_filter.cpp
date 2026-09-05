// SPDX-License-Identifier: Apache-2.0
#include "panels/log_filter.hpp"

#include <QVariant>

#include "panels/log_model.hpp"

namespace atp::studio::ui {

void log_filter::set_query(const log_query& query) {
    query_ = query;
    QT_WARNING_PUSH
    QT_WARNING_DISABLE_DEPRECATED
    invalidateRowsFilter();  // NOLINT(clang-diagnostic-deprecated-declarations)
    QT_WARNING_POP
}

bool log_filter::filterAcceptsRow(int row, const QModelIndex& parent) const {
    const QAbstractItemModel* source = sourceModel();
    if (source == nullptr) {
        return false;
    }
    const QModelIndex at = source->index(row, 0, parent);
    if (query_.origin.has_value()) {
        const QVariant origin = at.data(log_model::origin_role);
        if (!origin.isValid() || origin.toInt() != static_cast<int>(*query_.origin)) {
            return false;
        }
    }
    return query_.path.isEmpty() || at.data(log_model::path_role).toString() == query_.path;
}

}  // namespace atp::studio::ui
