// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_UI_LOG_ENTRY_HPP
#define ATP_STUDIO_UI_LOG_ENTRY_HPP

#include <chrono>

#include <QString>

#include <atp/module/log_level.hpp>

namespace atp::studio::ui {

/// Who wrote a log line.
///
/// It is a kind beside the path rather than a reserved word inside it, because a top-level module
/// may legitimately be named "system" — comparing kinds keeps the filter and the tabs from ever
/// confusing the two, while the drawn line is allowed to look the same.
enum class log_origin { system, module };

/// One line before it is drawn: what the writer said, when, and who the writer was.
///
/// A module's line arrives here from runtime::log_line and a studio's from main_window::report, and
/// the point of the type is that both then take the same path to the screen.
struct log_entry {
    std::chrono::system_clock::time_point at{};
    atp::log_level level = atp::log_level::info;
    log_origin origin = log_origin::system;

    /// The writing instance's dotted path, empty for a system line.
    QString path;

    QString text;
    bool truncated = false;
};

/// Draws one line the way a person reads it: "14:23:05.123 [warning] stage.counter: reconnected",
/// and "14:23:05.123 [info] system: saved" for the studio's own.
///
/// This is the only place a log line is drawn in the studio, and that is the point rather than
/// tidiness: the studio used to draw its own line in main_window and a module's in
/// runtime::format_log_line, two functions in two layers, and they had already drifted apart by the
/// very field — the source — that this one always writes.
/// @param entry the line
/// @return the rendered text, without a trailing newline
[[nodiscard]] QString render_log_line(const log_entry& entry);

}  // namespace atp::studio::ui

#endif
