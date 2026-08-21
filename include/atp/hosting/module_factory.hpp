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

/// What it takes to build TModule from arguments bound at registration: either the plain form, or the
/// same arguments followed by the per-instance config.
///
/// Named rather than written out at the class, because module_registry::add has to require **exactly**
/// this and not something stricter. It used to ask for the plain form alone, which rejected a module
/// whose only constructor takes a config — the very modules the channel exists for — so such a module
/// could be handed to module_factory directly but not registered by the ordinary call.
/// @tparam TModule module type
/// @tparam TArgs constructor argument types bound at registration
template <typename TModule, typename... TArgs>
concept factory_constructible = std::constructible_from<TModule, const TArgs&...> ||
                                std::constructible_from<TModule, const TArgs&..., const module_config&>;

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
        module_declaration decl = [this] {
            if constexpr (declares_ports<TModule>) {
                return declare_from_ports<typename TModule::ports_type>();
            } else {
                const module_ptr probe = create(module_config{});
                return declare_from_module(*probe);
            }
        }();
        if constexpr (declares_config<TModule>) {
            const typename TModule::config_type schema;
            decl.config_schema = schema.declared();
        }
        return decl;
    }

    /// A module that declares a config_type has its config validated here, before the module is built:
    /// the declared object is constructed once for the check and once more inside the module. The
    /// duplication is the price of validating at all — an unknown key is only knowable once every field
    /// has been declared, which is after the last member-initializer of the heir, where a module author
    /// using `using fields::fields;` has no code of their own to call from. The reward is that a bad
    /// config is refused before the module exists, so its failure never has to be told apart from a
    /// failure of the constructor.
    [[nodiscard]] module_ptr create(const module_config& cfg) const override {
        if constexpr (declares_config<TModule>) {
            const typename TModule::config_type checked(cfg);
            checked.throw_if_invalid();
        }
        return std::apply(
            [&cfg](const TArgs&... args) {
                if constexpr (std::constructible_from<TModule, const TArgs&..., const module_config&>) {
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