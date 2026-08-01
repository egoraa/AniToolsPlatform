#ifndef ANITOOLSPLATFORM_IO_INPUT_BASE_HPP
#define ANITOOLSPLATFORM_IO_INPUT_BASE_HPP

#include <any>
#include <typeindex>
#include <typeinfo>

#include <atp/io/io_base.hpp>

namespace atp::io {

/// Sink of the bare "something was delivered" signal, attached to an input by the executor to wake
/// a sleeping consumer thread without breaking pull-only reading: it carries no value.
class notifier_base {
   public:
    /// Called on the writer's thread right after delivery. Must be fast, must not throw and must
    /// not run user code.
    virtual void notify() noexcept = 0;

   protected:
    ~notifier_base() = default;
};

/// Type-erased base of an input: what the inputs registry stores, what output_base::connect takes
/// and what output<T> keeps in its subscriber list.
///
/// Defines the delivery protocol: the input itself answers which value types it accepts and how a
/// type-erased value is stored, so the output needs no hierarchy casts.
class input_base : public io_base {
   public:
    using io_base::io_base;

    /// Metadata of the produced type, supplied by the output: the type tag plus a boxing function.
    /// `box` is only used by universal inputs (input<std::any>); a typed input casts directly.
    struct erased_type {
        std::type_index type;
        std::any (*box)(const void*);
    };

    /// Per-type metadata instance. Every DLL holds its own copy, so erased_type addresses must
    /// never be compared — use the contents only.
    template <typename T>
    [[nodiscard]] static const erased_type& erased_of() {
        static const erased_type meta{typeid(T), [](const void* p) { return std::any(*static_cast<const T*>(p)); }};
        return meta;
    }

    /// Whether the input accepts values of type @p produced. Called once, at connect time, not per
    /// delivery.
    [[nodiscard]] virtual bool accepts(std::type_index produced) const = 0;

    /// Delivers a value and fires the notifier.
    /// @param value points at an object of type `meta.type`
    /// @param meta metadata of the produced type; the pairing with @p value is guaranteed by the
    ///        output and backed by the accepts() check performed at connect time
    void deliver(const void* value, const erased_type& meta) {
        do_deliver(value, meta);
        if (notifier_ != nullptr) {
            notifier_->notify();
        }
    }

    /// Installs or clears the delivery notifier. Setup phase only: strictly before the threads
    /// start and after they join — concurrent use with delivery is a race. Writing into the input
    /// directly does not fire it; the signal is about delivery from outputs.
    void set_notifier(notifier_base* notifier) noexcept {
        notifier_ = notifier;
    }

    /// Currently installed notifier, or nullptr.
    [[nodiscard]] notifier_base* notifier() const noexcept {
        return notifier_;
    }

   private:
    virtual void do_deliver(const void* value, const erased_type& meta) = 0;

    notifier_base* notifier_ = nullptr;
};

}  // namespace atp::io

#endif
