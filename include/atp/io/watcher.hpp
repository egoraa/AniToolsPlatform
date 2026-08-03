#ifndef ANITOOLSPLATFORM_IO_WATCHER_HPP
#define ANITOOLSPLATFORM_IO_WATCHER_HPP

#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include <atp/io/input.hpp>
#include <atp/io/property.hpp>
#include <atp/io/queued_input.hpp>
#include <atp/io/threading.hpp>

namespace atp::io {

/// Polling watcher: "input → handler" rules checked by a poll() call from the module's own thread.
/// It gives the ergonomics of a value callback while running the handler where the module lives, so
/// races with iterate are excluded by construction. Not thread-safe: watch() belongs to the setup
/// phase (initialize) and poll() to the module thread; watch the owning module's own ports.
class watcher {
   public:
    /// Rule for an input: a value arrived → take it and call the handler.
    /// type_identity_t switches off deducing T from the handler — T is fixed by the input, and the
    /// lambda merely converts to std::function.
    template <typename T>
    void watch(input<T>& in, std::type_identity_t<std::function<void(const T&)>> handler) {
        rules_.push_back(std::make_unique<value_rule<T>>(in, std::move(handler)));
    }

    /// Rule for a queueing input: the handler runs per element, and the queue is taken with
    /// drain(), one lock per pass — cheaper than the element-wise take of the base overload.
    template <typename T>
    void watch(queued_input<T>& in, std::type_identity_t<std::function<void(const T&)>> handler) {
        rules_.push_back(std::make_unique<queue_rule<T>>(in, std::move(handler)));
    }

    /// Rule for a property: the value changed (see property::take) → the handler runs. Reacting to
    /// settings is declared next to the rules for inputs.
    template <typename T>
    void watch(property<T>& prop, std::type_identity_t<std::function<void(const T&)>> handler) {
        rules_.push_back(std::make_unique<property_rule<T>>(prop, std::move(handler)));
    }

    /// Runs one pass over the rules in registration order.
    /// @return busy if at least one rule fired; hand it straight back from iterate:
    ///         `return watcher_.poll();`
    [[nodiscard]] work_status poll() {
        work_status pass = work_status::idle;
        for (const auto& rule : rules_) {
            if (rule->poll() == work_status::busy) {
                pass = work_status::busy;
            }
        }
        return pass;
    }

   private:
    struct rule_base {
        virtual ~rule_base() = default;
        virtual work_status poll() = 0;
    };

    template <typename T>
    struct value_rule final : rule_base {
        value_rule(input<T>& in, std::function<void(const T&)> handler) : in_(&in), handler_(std::move(handler)) {}
        work_status poll() override {
            std::optional<T> value = in_->take();
            if (!value) {
                return work_status::idle;
            }
            handler_(*value);
            return work_status::busy;
        }

       private:
        input<T>* in_;
        std::function<void(const T&)> handler_;
    };

    template <typename T>
    struct queue_rule final : rule_base {
        queue_rule(queued_input<T>& in, std::function<void(const T&)> handler)
            : in_(&in), handler_(std::move(handler)) {}
        work_status poll() override {
            auto batch = in_->drain();
            if (batch.empty()) {
                return work_status::idle;
            }
            for (const T& value : batch) {
                handler_(value);
            }
            return work_status::busy;
        }

       private:
        queued_input<T>* in_;
        std::function<void(const T&)> handler_;
    };

    template <typename T>
    struct property_rule final : rule_base {
        property_rule(property<T>& prop, std::function<void(const T&)> handler)
            : prop_(&prop), handler_(std::move(handler)) {}
        work_status poll() override {
            std::optional<T> value = prop_->take();
            if (!value) {
                return work_status::idle;
            }
            handler_(*value);
            return work_status::busy;
        }

       private:
        property<T>* prop_;
        std::function<void(const T&)> handler_;
    };

    std::vector<std::unique_ptr<rule_base>> rules_;
};

}  // namespace atp::io

#endif
