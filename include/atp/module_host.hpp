#ifndef ANITOOLSPLATFORM_MODULE_HOST_HPP
#define ANITOOLSPLATFORM_MODULE_HOST_HPP

#include <cstdint>
#include <string_view>

namespace atp {

/// Severity of a log line. Ordered from the most to the least important, which is the order the
/// hosts' thresholds compare against: a threshold admits every level up to and including itself.
enum class log_level : std::uint8_t { error, warning, info, debug };

/// The host's side of one module: everything the platform offers a module beyond its ports.
///
/// Handed out through module_context in initialize and valid for the module's whole life, including
/// after stop() — an OS callback firing during shutdown must find a live object rather than a
/// dangling one. Both operations are noexcept and allocation-free on the caller's side, because the
/// caller is iterate.
class module_host {
   public:
    module_host() = default;
    module_host(const module_host&) = delete;
    module_host& operator=(const module_host&) = delete;
    virtual ~module_host() = default;

    /// Writes one line. Never blocks and never throws: on a full buffer the line is dropped and
    /// counted, which is strictly better than stalling the pipeline for a diagnostic.
    /// @param level severity
    /// @param text the message; longer than the host's slot it is truncated, and the truncation is
    ///        visible in the output
    virtual void log(log_level level, std::string_view text) noexcept = 0;

    /// Asks the module's own thread to iterate now, from any thread. Carries no value and runs no
    /// user code — it is a wake-up, not a message. A no-op before start(), after stop(), and on a
    /// thread whose mode makes the notion meaningless (throttled, spinning).
    virtual void wake() noexcept = 0;

    /// @param text message written at the error level
    void error(std::string_view text) noexcept {
        log(log_level::error, text);
    }

    /// @param text message written at the warning level
    void warning(std::string_view text) noexcept {
        log(log_level::warning, text);
    }

    /// @param text message written at the info level
    void info(std::string_view text) noexcept {
        log(log_level::info, text);
    }

    /// @param text message written at the debug level
    void debug(std::string_view text) noexcept {
        log(log_level::debug, text);
    }
};

}  // namespace atp

#endif
