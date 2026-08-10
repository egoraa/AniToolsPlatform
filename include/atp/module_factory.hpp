// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_MODULE_FACTORY_HPP
#define ANITOOLSPLATFORM_MODULE_FACTORY_HPP

#include <concepts>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include <atp/module_base.hpp>
#include <atp/module_factory_base.hpp>
#include <atp/version.hpp>

namespace atp {

/// Contract "the module declares a version": a static module_version constant convertible to
/// version. A named concept rather than an inline requires keeps the if constexpr below on one line
/// (clang-format always breaks a compound requirement across several).
template <typename T>
concept has_module_version = requires {
    { T::module_version } -> std::convertible_to<version>;
};

/// Typed module factory.
///
/// Constructor arguments are bound at registration time: the factory stores them and every
/// create() builds an instance from the same values, so all instances of one factory are identical
/// and different configurations are separate registrations (aliases). Per-instance scalar settings do
/// not belong here but in the module's properties, applied to the created object.
///
/// The per-instance config goes **after** the bound arguments, and a module joins that channel only
/// if it declared a constructor taking one: a module that did not is built exactly as before, so
/// nothing existing changes by a line.
///
/// Hence the constraint is a disjunction rather than the plain "constructible from the bound
/// arguments" it used to be: a module whose only constructor takes a config is constructible from no
/// arguments at all, and requiring that form alone would reject exactly the modules this channel is
/// for. Both alternatives are named, so the class is still rejected when neither spelling fits, which
/// is the diagnostic worth keeping.
/// @tparam TModule module type
/// @tparam TArgs constructor argument types bound at registration
template <std::derived_from<module_base> TModule, typename... TArgs>
    requires(std::constructible_from<TModule, const TArgs&...> ||
             std::constructible_from<TModule, const TArgs&..., const config_value&>)
class module_factory final : public module_factory_base {
   public:
    /// @param name name to register the factory under
    /// @param args constructor arguments stored for every create()
    explicit module_factory(std::string name, TArgs... args) : name_(std::move(name)), args_(std::move(args)...) {}

    [[nodiscard]] std::string_view name() const noexcept override {
        return name_;
    }

    /// Reported statically from TModule::module_version, without creating an instance; a module written
    /// outside the module<> template and lacking the constant gets default_version.
    [[nodiscard]] version get_version() const noexcept override {
        if constexpr (has_module_version<TModule>) {
            return TModule::module_version;
        } else {
            return default_version;
        }
    }

    [[nodiscard]] module_ptr create(const config_value& cfg) const override {
        return std::apply(
            [&cfg](const TArgs&... args) {
                if constexpr (std::constructible_from<TModule, const TArgs&..., const config_value&>) {
                    return module_ptr(new TModule(args..., cfg), {});
                } else {
                    return module_ptr(new TModule(args...), {});
                }
            },
            args_);
    }

   private:
    std::string name_;
    std::tuple<TArgs...> args_;
};

}  // namespace atp

#endif