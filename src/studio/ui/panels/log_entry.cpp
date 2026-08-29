// SPDX-License-Identifier: Apache-2.0
#include "panels/log_entry.hpp"

#include <string_view>

#include <atp/runtime/log_pump.hpp>

namespace atp::studio::ui {

QString render_log_line(const log_entry& entry) {
    const std::string_view level = runtime::level_name(entry.level);
    QString out = QString::fromStdString(runtime::format_log_time(entry.at));
    out += QStringLiteral(" [");
    out += QString::fromUtf8(level.data(), static_cast<qsizetype>(level.size()));
    out += QStringLiteral("] ");
    out += entry.origin == log_origin::module ? entry.path : QStringLiteral("system");
    out += QStringLiteral(": ");
    out += entry.text;
    if (entry.truncated) {
        out += QStringLiteral("...");
    }
    return out;
}

}  // namespace atp::studio::ui
