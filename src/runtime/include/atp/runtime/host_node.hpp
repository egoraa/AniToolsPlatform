// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_RUNTIME_HOST_NODE_HPP
#define ATP_RUNTIME_HOST_NODE_HPP

#include <atomic>
#include <chrono>
#include <string>
#include <string_view>

#include <atp/io/input_base.hpp>
#include <atp/module/module_host.hpp>
#include <atp/runtime/log_ring.hpp>

namespace atp::runtime {

/// One drained log line, with the module that wrote it and the moment it did.
///
/// The stamp is taken by the writing module's own thread, not by whoever drains: a drain happens
/// on a timer, so its own clock would collapse a whole burst onto one instant and place it late.
/// It sits last so that the aggregate initialisation of the four fields that came before it keeps
/// compiling.
struct log_line {
    std::string path;
    log_level level = log_level::info;
    std::string text;
    bool truncated = false;
    std::chrono::system_clock::time_point at{};
};

/// The runtime's module_host: a log ring plus the notifier of the thread that runs the module.
///
/// It lives next to the module in group::child, so it outlives every run: start() points wake() at
/// the thread's notifier, stop() clears it, and in between the two — or on a thread whose mode has
/// no use for the notion — wake() does nothing at all. That is precisely why the object is not
/// owned by the runner: an OS callback firing during shutdown must find it alive.
class host_node final : public module_host {
   public:
    void log(log_level level, std::string_view text) noexcept override {
        ring_.write(level, text);
    }

    void wake() noexcept override {
        if (io::notifier_base* target = notifier_.load(std::memory_order_acquire)) {
            target->notify();
        }
    }

    /// Points wake() at a thread, or nowhere when the argument is nullptr. Called by the runner on
    /// start and on stop, never by the module.
    /// @param target the thread's notifier, or nullptr to detach
    void attach(io::notifier_base* target) noexcept {
        notifier_.store(target, std::memory_order_release);
    }

    /// The buffer, for the host draining it.
    [[nodiscard]] log_ring& ring() noexcept {
        return ring_;
    }

   private:
    log_ring ring_;
    std::atomic<io::notifier_base*> notifier_{nullptr};
};

}  // namespace atp::runtime

#endif
