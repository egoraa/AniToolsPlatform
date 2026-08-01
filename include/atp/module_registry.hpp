#ifndef ANITOOLSPLATFORM_MODULE_REGISTRY_HPP
#define ANITOOLSPLATFORM_MODULE_REGISTRY_HPP

#include <concepts>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <atp/module_factory.hpp>

namespace atp {

/// Contract "the module declares a name of its own": a static module_name member convertible to
/// string_view and non-empty (module<> derives it from the NTTP; a module written outside the
/// template declares it by hand). An anonymous module does not satisfy the concept and can only be
/// registered through an explicit add<M>(name).
template <typename T>
concept has_module_name = requires {
    { T::module_name } -> std::convertible_to<std::string_view>;
} && (!std::string_view{T::module_name}.empty());

/// Owning registry of module factories. One name may hold several versions: a factory is keyed by
/// the pair (name, version), the version being taken from the factory itself, and a request without
/// a version means the latest (highest) one.
///
/// The API deliberately mirrors the io registries (at/find/remove/list, the same error contracts)
/// without reusing detail::io_registry, which is tied to io_base and the (name, safety) constructor
/// signature. Not thread-safe — registration belongs to the setup phase.
class module_registry {
   public:
    module_registry() = default;

    module_registry(const module_registry&) = delete;
    module_registry& operator=(const module_registry&) = delete;

    /// Registers a module under the name it declares itself (contract has_module_name).
    /// @throws std::runtime_error if this name and version are already registered
    template <std::derived_from<module_base> M>
        requires std::constructible_from<M> && has_module_name<M>
    module_factory_base& add() {
        return add<M>(std::string{M::module_name});
    }

    /// Registers a module under an explicit name, so one type may be registered under aliases.
    /// @param name registration name
    /// @param args constructor arguments bound to the factory
    /// @throws std::runtime_error if this name and version are already registered
    template <std::derived_from<module_base> M, typename... TArgs>
        requires std::constructible_from<M, const std::decay_t<TArgs>&...>
    module_factory_base& add(std::string name, TArgs&&... args) {
        return add(
            std::make_unique<module_factory<M, std::decay_t<TArgs>...>>(std::move(name), std::forward<TArgs>(args)...));
    }

    /// Registers a ready-made factory — the shared path, also open to non-standard factories. A
    /// duplicate means both the name and the version match; one name with several versions is
    /// normal.
    /// @throws std::invalid_argument on a null factory
    /// @throws std::runtime_error if this name and version are already registered
    module_factory_base& add(std::unique_ptr<module_factory_base> factory) {
        if (!factory) {
            throw std::invalid_argument("null module factory");
        }
        module_factory_base& ref = *factory;
        auto& versions = registry_[std::string(ref.name())];
        auto [it, inserted] = versions.try_emplace(ref.get_version(), std::move(factory));
        if (!inserted) {
            throw std::runtime_error("duplicate module '" + std::string(ref.name()) + "' version '" +
                                     ref.get_version().to_string() + "'");
        }
        return ref;
    }

    /// Creates a module of the latest (highest) registered version of this name.
    /// @throws std::runtime_error if the name is unknown
    [[nodiscard]] module_ptr create(const std::string& name) const {
        return at(name).create();
    }

    /// Creates a module of an exact version (1.2 == 1.2.0: zero padding).
    /// @throws std::runtime_error if the name or the version is unknown
    [[nodiscard]] module_ptr create(const std::string& name, const version& v) const {
        return at(name, v).create();
    }

    /// Factory of the latest registered version of this name.
    /// @throws std::runtime_error if the name is unknown
    [[nodiscard]] module_factory_base& at(const std::string& name) const {
        module_factory_base* factory = find(name);
        if (!factory) {
            throw std::runtime_error("no module named '" + name + "'");
        }
        return *factory;
    }

    /// Factory of an exact version.
    /// @throws std::runtime_error if the name is unknown, or the name has no such version — the two
    ///         cases carry different messages
    [[nodiscard]] module_factory_base& at(const std::string& name, const version& v) const {
        auto it = registry_.find(name);
        if (it == registry_.end()) {
            throw std::runtime_error("no module named '" + name + "'");
        }
        auto found = it->second.find(v);
        if (found == it->second.end()) {
            throw std::runtime_error("module '" + name + "' has no version '" + v.to_string() + "'");
        }
        return *found->second;
    }

    /// Factory of the latest registered version of this name; nullptr if the name is unknown.
    [[nodiscard]] module_factory_base* find(const std::string& name) const {
        auto it = registry_.find(name);
        if (it == registry_.end()) {
            return nullptr;
        }
        return it->second.rbegin()->second.get();
    }

    /// Factory of an exact version; nullptr if the name or the version is unknown.
    [[nodiscard]] module_factory_base* find(const std::string& name, const version& v) const {
        auto it = registry_.find(name);
        if (it == registry_.end()) {
            return nullptr;
        }
        auto found = it->second.find(v);
        return found == it->second.end() ? nullptr : found->second.get();
    }

    /// Versions registered under a name, ascending; an unknown name yields an empty vector —
    /// enumerating is not an error.
    [[nodiscard]] std::vector<version> versions(const std::string& name) const {
        std::vector<version> result;
        auto it = registry_.find(name);
        if (it == registry_.end()) {
            return result;
        }
        result.reserve(it->second.size());
        for (const auto& [ver, factory] : it->second) {
            result.push_back(ver);
        }
        return result;
    }

    /// Removes every version registered under a name.
    /// @return false if the name was unknown
    bool remove(const std::string& name) {
        return registry_.erase(name) > 0;
    }

    /// Removes a single version. An entry left without versions is erased whole, keeping the
    /// "inner map is never empty" invariant.
    /// @return false if there was no such name or version
    bool remove(const std::string& name, const version& v) {
        auto it = registry_.find(name);
        if (it == registry_.end()) {
            return false;
        }
        if (it->second.erase(v) == 0) {
            return false;
        }
        if (it->second.empty()) {
            registry_.erase(it);
        }
        return true;
    }

    /// Every registered factory, all names and versions alike.
    [[nodiscard]] std::vector<const module_factory_base*> list() const {
        std::vector<const module_factory_base*> result;
        for (const auto& [name, versions] : registry_) {
            for (const auto& [ver, factory] : versions) {
                result.push_back(factory.get());
            }
        }
        return result;
    }

   private:
    std::unordered_map<std::string, std::map<version, std::unique_ptr<module_factory_base>>> registry_;
};

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
    [[nodiscard]] module_ptr create() const override {
        module_ptr m = inner_->create();
        return module_ptr(m.release(), module_deleter{pin_});
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
class module_registrar {
   public:
    /// @param registry registry the factories go into
    /// @param pin plugin library to pin: the factories are wrapped so that every module created
    ///        holds it against unloading. Monolithic registration passes no pin.
    explicit module_registrar(module_registry& registry, std::shared_ptr<void> pin = {})
        : registry_(&registry), pin_(std::move(pin)) {}

    /// Registers a module under the name it declares itself (contract has_module_name).
    /// @throws std::runtime_error if this name and version are already registered
    template <std::derived_from<module_base> M>
        requires std::constructible_from<M> && has_module_name<M>
    module_factory_base& add() {
        return add<M>(std::string{M::module_name});
    }

    /// Registers a module under an explicit name.
    /// @param name registration name
    /// @param args constructor arguments bound to the factory
    /// @throws std::runtime_error if this name and version are already registered
    template <std::derived_from<module_base> M, typename... TArgs>
        requires std::constructible_from<M, const std::decay_t<TArgs>&...>
    module_factory_base& add(std::string name, TArgs&&... args) {
        return add(
            std::make_unique<module_factory<M, std::decay_t<TArgs>...>>(std::move(name), std::forward<TArgs>(args)...));
    }

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
