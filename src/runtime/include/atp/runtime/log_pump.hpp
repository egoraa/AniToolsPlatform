// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_RUNTIME_LOG_PUMP_HPP
#define ATP_RUNTIME_LOG_PUMP_HPP

#include <atomic>
#include <chrono>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <atp/module/log_level.hpp>
#include <atp/runtime/host_node.hpp>
#include <atp/runtime/pipeline.hpp>

namespace atp::runtime {

/// Where drained lines go. Called on the pump's thread, one line at a time.
using log_sink = std::function<void(const log_line&)>;

/// Where they come from. A function rather than a pipeline reference, because atp_mcp has no
/// pipeline until a tool call builds one: the source answers an empty vector until there is
/// something to drain, and the pump neither knows nor cares.
using log_source = std::function<std::vector<log_line>()>;

/// The word a level is written as.
/// @param level the level
/// @return its token
[[nodiscard]] inline std::string_view level_name(log_level level) noexcept {
    switch (level) {
        case log_level::error:
            return "error";
        case log_level::warning:
            return "warning";
        case log_level::debug:
            return "debug";
        case log_level::info:
            break;
    }
    return "info";
}

/// The reverse. An unknown word yields nothing rather than a guess: a command line saying --log
/// loud has to be refused, not silently read as something else.
/// @param name token from a command line
/// @return the level, or nullopt when the word is not one of them
[[nodiscard]] inline std::optional<log_level> level_from_name(std::string_view name) noexcept {
    if (name == "error") {
        return log_level::error;
    }
    if (name == "warning") {
        return log_level::warning;
    }
    if (name == "info") {
        return log_level::info;
    }
    if (name == "debug") {
        return log_level::debug;
    }
    return std::nullopt;
}

/// The moment as a person reads it: "14:23:05.123", local time to the millisecond.
///
/// No date, because a log is read within the day it was written and a date would cost width on
/// every single line; milliseconds, because the drain that carries the line to a console or a dock
/// runs tens of times a second and a finer figure would say more than the reader can use — the
/// order inside one burst is already faithful, the ring hands its lines over oldest first.
///
/// A machine with no time zone database (a bare container, typically) makes the conversion throw;
/// the stamp then falls back to UTC rather than taking the whole log down with it, since a
/// formatter that throws while something is being reported destroys the report as well.
/// @param at the moment, as recorded by the writing thread
/// @return the rendered stamp
[[nodiscard]] inline std::string format_log_time(std::chrono::system_clock::time_point at) {
    const auto stamp = std::chrono::floor<std::chrono::milliseconds>(at);
    try {
        return std::format("{:%H:%M:%S}", std::chrono::zoned_time(std::chrono::current_zone(), stamp));
    } catch (...) {
        return std::format("{:%H:%M:%S}", stamp);
    }
}

/// One line as a person reads it: "14:23:05.123 [warning] stage.counter: device reconnected". A
/// truncated message ends in an ellipsis, because a loss that is invisible is worse than the loss
/// itself.
/// @param line the drained record
/// @return the rendered line, without a trailing newline
[[nodiscard]] inline std::string format_log_line(const log_line& line) {
    std::string text = format_log_time(line.at);
    text += " [";
    text += level_name(line.level);
    text += "] ";
    text += line.path;
    text += ": ";
    text += line.text;
    if (line.truncated) {
        text += "...";
    }
    return text;
}

/// Drains log buffers on a thread of its own.
///
/// It exists because the console hosts have no thread to spare: atp_app blocks in runner.wait()
/// until the first error and atp_mcp blocks reading stdin. The studio needs none of this — it
/// drains from the timer it already runs.
///
/// Walking the tree while the pipeline runs is safe for the same reason the runner relies on: the
/// structure of a running pipeline does not change.
class log_pump {
   public:
    /// Starts pumping immediately.
    /// @param source where lines are drained from; called only on the pump's thread
    /// @param sink where they go
    /// @param interval how often to drain
    log_pump(log_source source, log_sink sink, std::chrono::milliseconds interval = std::chrono::milliseconds(50))
        : source_(std::move(source)), sink_(std::move(sink)), interval_(interval) {
        worker_ = std::thread([this] { run(); });
    }

    /// Convenience for a host that owns its pipeline outright.
    /// @param pipe must outlive the pump
    /// @param sink where the lines go
    /// @param interval how often to drain
    log_pump(pipeline& pipe, log_sink sink, std::chrono::milliseconds interval = std::chrono::milliseconds(50))
        : log_pump([&pipe] { return pipe.collect_logs(); }, std::move(sink), interval) {}

    log_pump(const log_pump&) = delete;
    log_pump& operator=(const log_pump&) = delete;

    /// Stops the thread and drains once more: the last lines before a shutdown are the ones worth
    /// reading, and they are written after the final scheduled pass.
    ~log_pump() {
        stopping_.store(true, std::memory_order_relaxed);
        if (worker_.joinable()) {
            worker_.join();
        }
        drain_once();
    }

   private:
    void run() {
        while (!stopping_.load(std::memory_order_relaxed)) {
            drain_once();
            std::this_thread::sleep_for(interval_);
        }
    }

    void drain_once() {
        for (const log_line& line : source_()) {
            sink_(line);
        }
    }

    log_source source_;
    log_sink sink_;
    std::chrono::milliseconds interval_;
    std::atomic<bool> stopping_{false};
    std::thread worker_;
};

}  // namespace atp::runtime

#endif
