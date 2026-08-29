// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_UI_LOG_MODEL_HPP
#define ATP_STUDIO_UI_LOG_MODEL_HPP

#include <deque>

#include <QAbstractListModel>
#include <QString>
#include <QStringList>
#include <QVariant>

#include "panels/log_entry.hpp"

namespace atp::studio::ui {

/// The whole log, once, behind every view of it.
///
/// It exists because the dock used to be a QListWidget of finished strings, which is a view with no
/// model at all: the level was spent on a colour and forgotten and the source was never stored, so
/// there was nothing to filter and nothing to give a tab. A tab is now a predicate over this rather
/// than a history of its own — one history is what makes eviction, clearing and the list of known
/// sources agree across every open tab without any of them talking to each other.
class log_model final : public QAbstractListModel {
   public:
    /// Lines kept before the oldest are dropped.
    ///
    /// A log worth filtering is one whose history survives, and a history that survives without a
    /// ceiling is a leak: a talkative module writes hundreds of lines a second, and a record here
    /// costs more than the bare string the dock used to keep.
    static constexpr int max_lines = 20000;

    /// The kind of writer, as an int of log_origin. A role rather than a getter, so that log_filter
    /// can be written against a model it does not name.
    static constexpr int origin_role = Qt::UserRole;

    /// The writing instance's dotted path, empty for a system line.
    static constexpr int path_role = Qt::UserRole + 1;

    explicit log_model(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    /// Draws the line and puts it at the end, dropping the oldest if that takes the log past
    /// max_lines.
    /// @param entry the line
    void append(const log_entry& entry);

    /// Empties the log and forgets every path it has seen.
    void clear();

    /// The instances that have written something, in the order they first did.
    ///
    /// Eviction deliberately does not take a path off this list: a module whose lines have all been
    /// dropped is still a source one may open a view on, it is merely an empty one. Only clear()
    /// forgets.
    /// @return the paths, without duplicates
    [[nodiscard]] QStringList paths() const {
        return paths_;
    }

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;

    /// Answers the drawn line, the colour its severity earns, and the two keys anything filters on.
    ///
    /// A calm line answers no colour at all rather than the palette's own: colouring every line
    /// would leave nothing standing out, and what is copied out of the log is the text, which names
    /// the level in words anyway.
    /// @param index the line
    /// @param role Qt::DisplayRole, Qt::ForegroundRole, origin_role or path_role
    /// @return the value, or an invalid variant for anything else
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;

   private:
    /// One line as it is kept: the drawn text plus the three keys anything asks it about.
    ///
    /// The moment, the raw message and the truncation flag are deliberately absent — they are
    /// already inside the drawn text, and a second copy of them would be a second copy.
    struct record {
        QString line;
        QString path;
        log_origin origin = log_origin::system;
        atp::log_level level = atp::log_level::info;
    };

    std::deque<record> lines_;
    QStringList paths_;
};

}  // namespace atp::studio::ui

#endif
