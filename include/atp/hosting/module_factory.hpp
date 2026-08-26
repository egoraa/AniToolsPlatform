// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_HOSTING_MODULE_FACTORY_HPP
#define ANITOOLSPLATFORM_HOSTING_MODULE_FACTORY_HPP

#include <concepts>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include <atp/hosting/module_factory_base.hpp>
#include <atp/module/module_base.hpp>
#include <atp/support/version.hpp>

namespace atp {

/// Contract "the module declares a version": a static module_version constant convertible to
/// version. A named concept rather than an inline requires keeps the if constexpr below on one line
/// (clang-format always breaks a compound requirement across several).
template <typename T>
concept has_module_version = requires {
    { T::module_version } -> std::convertible_to<version>;
};

/// Contract "this module declares its config": a config_type that is a module_config heir and can be
/// built with no arguments, which is how its structure is read without a module.
///
/// Declaring it is also what puts the module's config through the host's check: a module that reads a
/// tree by hand names no config_type and is left exactly as it was.
template <typename TModule>
concept declares_config =
    requires { typename TModule::config_type; } && std::derived_from<typename TModule::config_type, module_config> &&
    std::default_initializable<typename TModule::config_type>;

/// Contract "this module takes its config in its constructor": the bound arguments followed by
/// ownership of the declared config.
///
/// A concept rather than a bool written out at the use site, because the two halves have to be
/// checked in this order — a `&&` of two expressions would substitute TModule::config_type even for a
/// module that names none, which is a hard error rather than a false.
/// @tparam TModule module type
/// @tparam TArgs constructor argument types bound at registration
template <typename TModule, typename... TArgs>
concept takes_config =
    declares_config<TModule> &&
    std::constructible_from<TModule, const TArgs&..., std::unique_ptr<typename TModule::config_type>>;

/// What it takes to build TModule from arguments bound at registration: either the plain form, or the
/// same arguments followed by ownership of the per-instance config.
///
/// Named rather than written out at the class, because module_registry::add has to require **exactly**
/// this and not something stricter. It used to ask for the plain form alone, which rejected a module
/// whose only constructor takes a config — the very modules the channel exists for — so such a module
/// could be handed to module_factory directly but not registered by the ordinary call.
/// @tparam TModule module type
/// @tparam TArgs constructor argument types bound at registration
template <typename TModule, typename... TArgs>
concept factory_constructible = std::constructible_from<TModule, const TArgs&...> || takes_config<TModule, TArgs...>;

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
    requires factory_constructible<TModule, TArgs...>
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

    /// Built from TModule::ports_type when the module names one — the constructor is not called, so a
    /// heavy or a config-hungry module costs nothing to describe. A module written by hand from
    /// module_base names no node, and for it the old probe is still the only answer; the choice is
    /// made at compile time, so neither path pays for the other.
    [[nodiscard]] module_declaration declaration() const override {
        if constexpr (declares_ports<TModule>) {
            return declare_from_ports<typename TModule::ports_type>();
        } else {
            const module_ptr probe = create(make_config());
            return declare_from_module(*probe);
        }
    }

    /// The module's own config type at its declared defaults, or nothing when the module declares
    /// none. Built here and handed back type-erased, which is what lets a host fill it through
    /// module_config::entry without ever naming the type.
    [[nodiscard]] config_ptr make_config() const override {
        if constexpr (declares_config<TModule>) {
            return config_ptr(new typename TModule::config_type, config_deleter{});
        } else {
            return {};
        }
    }

    /// The config must be the one this factory made: it is cast back to the module's own type, and a
    /// config of another module is refused rather than reinterpreted.
    ///
    /// Ownership passes into the module, and the deleter's pin is dropped with it on purpose — from
    /// here on the config lives inside the module, and the module holds the library up with a pin of
    /// its own.
    [[nodiscard]] module_ptr create(config_ptr config) const override {
        return std::apply(
            [&](const TArgs&... args) {
                if constexpr (takes_config<TModule, TArgs...>) {
                    using config_type = typename TModule::config_type;
                    if (dynamic_cast<config_type*>(config.get()) == nullptr) {
                        throw config::access_error("factory '" + name_ + "' was handed a config of another module");
                    }
                    std::unique_ptr<config_type> owned(static_cast<config_type*>(config.release()));
                    return module_ptr(new TModule(args..., std::move(owned)), {});
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