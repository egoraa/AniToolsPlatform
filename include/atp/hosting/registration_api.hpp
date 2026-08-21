// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_HOSTING_REGISTRATION_API_HPP
#define ANITOOLSPLATFORM_HOSTING_REGISTRATION_API_HPP

#include <concepts>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <atp/hosting/module_factory.hpp>

namespace atp {

/// Contract "the module declares a name of its own": a static module_name member convertible to
/// string_view and non-empty (module<> derives it from the NTTP; a module written outside the
/// template declares it by hand). An anonymous module does not satisfy the concept and can only be
/// registered through an explicit add<TModule>(name).
template <typename T>
concept has_module_name = requires {
    { T::module_name } -> std::convertible_to<std::string_view>;
} && (!std::string_view{T::module_name}.empty());

namespace detail {

/// Shared front end of the two registration surfaces. module_registry and module_registrar offer the
/// same three add() spellings and differ only in what happens to a ready-made factory — the registry
/// stores it, the registrar wraps it into the plugin pin and remembers the pair. Writing the two
/// templates once means a third spelling cannot appear on one and be missing from the other.
///
/// An heir must offer add(std::unique_ptr<module_factory_base>) and pull these overloads in with
/// `using`: its own add() would otherwise hide them, since name lookup stops at the first scope that
/// has the name.
/// @tparam TSelf heir, reached without a virtual call
template <typename TSelf>
class registration_api {
   public:
    /// Registers a module under the name it declares itself (contract has_module_name).
    /// @throws std::runtime_error if this name and version are already registered
    template <std::derived_from<module_base> TModule>
        requires factory_constructible<TModule> && has_module_name<TModule>
    module_factory_base& add() {
        return add<TModule>(std::string{TModule::module_name});
    }

    /// Registers a module under an explicit name, so one type may be registered under aliases.
    /// @param name registration name
    /// @param args constructor arguments bound to the factory
    /// @throws std::runtime_error if this name and version are already registered
    template <std::derived_from<module_base> TModule, typename... TArgs>
        requires factory_constructible<TModule, std::decay_t<TArgs>...>
    module_factory_base& add(std::string name, TArgs&&... args) {
        return static_cast<TSelf&>(*this).add(std::make_unique<module_factory<TModule, std::decay_t<TArgs>...>>(
            std::move(name), std::forward<TArgs>(args)...));
    }

   protected:
    /// Constructible and destructible only as a base, and never copied. Both matter because the
    /// downcast is unchecked: a standalone or sliced registration_api would send add() through a
    /// static_cast to a derived object that does not exist, and the registry pointer it reads would
    /// be whatever the memory held. The heir is not polymorphic, so the destructor is non-virtual on
    /// purpose — nothing here is ever deleted through this type.
    registration_api() = default;
    ~registration_api() = default;
    registration_api(const registration_api&) = delete;
    registration_api& operator=(const registration_api&) = delete;
};

}  // namespace detail

}  // namespace atp

#endif
