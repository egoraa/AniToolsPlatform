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
/// and different configurations are separate registrations (aliases). Per-instance settings do not
/// belong here but in the module's properties, applied to the created object.
/// @tparam TModule module type
/// @tparam TArgs constructor argument types bound at registration
template <std::derived_from<module_base> TModule, typename... TArgs>
    requires std::constructible_from<TModule, const TArgs&...>
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

    [[nodiscard]] module_ptr create() const override {
        return std::apply([](const TArgs&... args) { return module_ptr(new TModule(args...), {}); }, args_);
    }

   private:
    std::string name_;
    std::tuple<TArgs...> args_;
};

}  // namespace atp

#endif