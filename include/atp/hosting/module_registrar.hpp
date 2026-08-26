// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_HOSTING_MODULE_REGISTRAR_HPP
#define ANITOOLSPLATFORM_HOSTING_MODULE_REGISTRAR_HPP

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <atp/hosting/module_registry.hpp>
#include <atp/hosting/registration_api.hpp>

namespace atp {

namespace detail {

class pinned_factory final : public module_factory_base {
   public:
    pinned_factory(std::unique_ptr<module_factory_base> inner, std::shared_ptr<void> pin)
        : pin_(std::move(pin)), inner_(std::move(inner)) {}

    [[nodiscard]] std::string_view name() const noexcept override {
        return inner_->name();
    }
    [[nodiscard]] version get_version() const noexcept override {
        return inner_->get_version();
    }
    [[nodiscard]] config_ptr make_config() const override {
        config_ptr inner = inner_->make_config();
        return inner ? config_ptr(inner.release(), config_deleter{pin_}) : config_ptr{};
    }
    [[nodiscard]] module_ptr create(config_ptr config) const override {
        module_ptr m = inner_->create(config_ptr(config.release(), config_deleter{}));
        return module_ptr(m.release(), module_deleter{pin_});
    }
    [[nodiscard]] module_declaration declaration() const override {
        return inner_->declaration();
    }

   private:
    std::shared_ptr<void> pin_;
    std::unique_ptr<module_factory_base> inner_;
};

}  // namespace detail

/// Thin wrapper over a registry that also remembers the (name, version) pairs registered through
/// it. Module registration functions take this rather than the registry itself, so that
/// module_loader knows which factories its own library brought and can withdraw them on unload
/// without touching other versions of the same names. A concrete class, not a virtual one: the
/// header-only platform is instantiated afresh in every participant.
class module_registrar : public detail::registration_api<module_registrar> {
   public:
    /// Registers a module through this registrar — the very spellings a plugin's
    /// atp_register_modules calls — either under the name the module declares itself or under an
    /// explicit one with constructor arguments bound to the factory. Both are written once in
    /// detail::registration_api and shared with module_registry, and both end up in the pin and in
    /// registered() because they route through the add() below.
    /// @throws std::runtime_error if this name and version are already registered
    using detail::registration_api<module_registrar>::add;

    /// @param registry registry the factories go into
    /// @param pin plugin library to pin: the factories are wrapped so that every module created
    ///        holds it against unloading. Monolithic registration passes no pin.
    explicit module_registrar(module_registry& registry, std::shared_ptr<void> pin = {})
        : registry_(&registry), pin_(std::move(pin)) {}

    /// Registers a ready-made factory, wrapping it into the pin when there is one.
    /// @throws std::invalid_argument on a null factory
    /// @throws std::runtime_error if this name and version are already registered
    module_factory_base& add(std::unique_ptr<module_factory_base> factory) {
        if (pin_) {
            factory = std::make_unique<detail::pinned_factory>(std::move(factory), pin_);
        }
        module_factory_base& ref = registry_->add(std::move(factory));
        registered_.emplace_back(std::string(ref.name()), ref.get_version());
        return ref;
    }

    /// Pairs registered through this registrar, in registration order.
    [[nodiscard]] const std::vector<std::pair<std::string, version>>& registered() const {
        return registered_;
    }

   private:
    module_registry* registry_;
    std::shared_ptr<void> pin_;
    std::vector<std::pair<std::string, version>> registered_;
};

}  // namespace atp

#endif
