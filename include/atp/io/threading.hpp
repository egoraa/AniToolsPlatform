#ifndef ANITOOLSPLATFORM_IO_THREADING_HPP
#define ANITOOLSPLATFORM_IO_THREADING_HPP

namespace atp::io {

/// Thread-safety of a single io element, chosen at construction time:
/// `make<input<int>>("fast", unsafe)`.
struct safety {
    bool locking;
};

/// Locking instance: reads and writes are serialised by the element's mutex.
inline constexpr safety safe{true};

/// Non-locking instance: cheaper, but usable from one thread only.
inline constexpr safety unsafe{false};

/// Answer of a polled entity (module_base::iterate, watcher::poll): whether it did any work.
/// A thread whose modules all report `idle` lets the runner back off; `busy` resets the backoff.
enum class work_status { busy, idle };

}  // namespace atp::io

#endif  // ANITOOLSPLATFORM_IO_THREADING_HPP
