// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_IO_IO_BASE_HPP
#define ANITOOLSPLATFORM_IO_IO_BASE_HPP

#include <cstddef>
#include <mutex>
#include <string>
#include <typeindex>
#include <utility>

#include <atp/io/threading.hpp>

namespace atp::io {

/// Size above which the io layer materialises a value on the heap instead of the stack.
///
/// A port may carry a value of any size: no path of a write puts more than this on the stack, so a
/// megabyte frame is a legal port type rather than a stack overflow inside the writer's thread.
/// Above a page the allocation is under a percent of the copy it accompanies; below it, a noticeable
/// share of it — which is why the boundary sits at a page rather than at zero.
inline constexpr std::size_t heap_copy_threshold = 4096;

/// Common type-erased base of every declared io element: name, value type and synchronisation.
///
/// Owns the mutex shared by all element kinds; locking is enabled per instance at construction.
/// The thin heirs input_base/output_base exist for type separation alone, so that a registry of
/// inputs cannot physically accept an output and vice versa.
class io_base {
   public:
    /// @param name element name, unique within its registry
    /// @param type typeid of the transported value
    /// @param s whether this instance serialises access
    io_base(std::string name, std::type_index type, safety s)
        : name_(std::move(name)), type_(type), locking_(s.locking) {}
    virtual ~io_base() = default;

    io_base(const io_base&) = delete;
    io_base& operator=(const io_base&) = delete;

    /// Name the element was declared with.
    [[nodiscard]] const std::string& name() const {
        return name_;
    }

    /// typeid of the transported value.
    [[nodiscard]] std::type_index type() const {
        return type_;
    }

    /// Whether this instance serialises access. The runner validates cross-thread connections
    /// against this flag.
    [[nodiscard]] bool thread_safe() const noexcept {
        return locking_;
    }

    /// Drops the accumulated state: an input forgets what it holds and its counters, an output its
    /// write count, a property returns to its default.
    ///
    /// **Not "as if just constructed"** — a property additionally raises its changed flag, because a
    /// module has to learn about the rollback, and that is the whole point of resetting one. The
    /// platform itself calls this nowhere: the runtime keeps its counters monotonic across runs on
    /// purpose (see pipeline_runner::stats), so this exists for a host or a test that means to start
    /// a port over.
    virtual void reset() = 0;

   protected:
    /// Lock guarding this element's state; a deferred, no-op lock for unsafe instances.
    [[nodiscard]] std::unique_lock<std::mutex> lock() const {
        return locking_ ? std::unique_lock(mutex_) : std::unique_lock(mutex_, std::defer_lock);
    }

   private:
    std::string name_;
    std::type_index type_;
    mutable std::mutex mutex_;
    const bool locking_;
};

}  // namespace atp::io

#endif
