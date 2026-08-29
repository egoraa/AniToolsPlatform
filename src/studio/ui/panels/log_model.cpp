// SPDX-License-Identifier: Apache-2.0
#include "panels/log_model.hpp"

#include <cstddef>

#include <QBrush>
#include <QColor>

namespace atp::studio::ui {

void log_model::append(const log_entry& entry) {
    const int row = static_cast<int>(lines_.size());
    beginInsertRows(QModelIndex(), row, row);
    lines_.push_back({render_log_line(entry), entry.path, entry.origin, entry.level});
    endInsertRows();

    if (entry.origin == log_origin::module && !entry.path.isEmpty() && !paths_.contains(entry.path)) {
        paths_.push_back(entry.path);
    }

    const int extra = static_cast<int>(lines_.size()) - max_lines;
    if (extra > 0) {
        beginRemoveRows(QModelIndex(), 0, extra - 1);
        lines_.erase(lines_.begin(), lines_.begin() + extra);
        endRemoveRows();
    }
}

void log_model::clear() {
    beginResetModel();
    lines_.clear();
    paths_.clear();
    endResetModel();
}

int log_model::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(lines_.size());
}

QVariant log_model::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(lines_.size())) {
        return {};
    }
    const record& line = lines_[static_cast<std::size_t>(index.row())];
    switch (role) {
        case Qt::DisplayRole:
            return line.line;
        case Qt::ForegroundRole:
            if (line.level == atp::log_level::error) {
                return QBrush(QColor(220, 80, 80));
            }
            if (line.level == atp::log_level::warning) {
                return QBrush(QColor(220, 170, 80));
            }
            return {};
        case origin_role:
            return static_cast<int>(line.origin);
        case path_role:
            return line.path;
        default:
            return {};
    }
}

}  // namespace atp::studio::ui
