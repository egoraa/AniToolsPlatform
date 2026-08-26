// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_HOSTING_MODULE_REGISTRY_HPP
#define ANITOOLSPLATFORM_HOSTING_MODULE_REGISTRY_HPP

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <atp/hosting/module_factory.hpp>
#include <atp/hosting/registration_api.hpp>

namespace atp {

/// Owning registry of module factories. One name may hold several versions: a factory is keyed by
/// the pair (name, version), the version being taken from the factory itself, and a request without
/// a version means the latest (highest) one.
///
/// The API deliberately mirrors the io registries (at/find/remove/list, the same error contracts)
/// without reusing detail::io_registry, which is tied to io_base and the (name, safety) constructor
/// signature. Not thread-safe — registration belongs to the setup phase.
class module_registry : public detail::registration_api<module_registry> {
   public:
    /// Registers a module into this registry, either under the name it declares itself or under an
    /// explicit one with constructor arguments bound to the factory. Both spellings are written once
    /// in detail::registration_api and shared with module_registrar.
    /// @throws std::runtime_error if this name and version are already registered
    using detail::registration_api<module_registry>::add;

    module_registry() = default;

    module_registry(const module_registry&) = delete;
    module_registry& operator=(const module_registry&) = delete;

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
    [[nodiscard]] module_ptr create(const std::string& name, config_ptr config) const {
        return at(name).create(std::move(config));
    }

    /// Creates a module of an exact version (1.2 == 1.2.0: zero padding).
    /// @throws std::runtime_error if the name or the version is unknown
    [[nodiscard]] module_ptr create(const std::string& name, const version& v, config_ptr config) const {
        return at(name, v).create(std::move(config));
    }

    /// Builds the module with a config at its declared defaults and **does not fill it**: filling
    /// means reading a document, and there is none here. That job, and the check that goes with it,
    /// belong to the host and live in runtime::config_binding.
    ///
    /// For the callers that have no document to give — tests and studio's add_module among them. The
    /// convenience lives here rather than as a default argument on module_factory_base::create,
    /// because on a virtual function a default is taken from the static type of the call, which is a
    /// classic trap. It is not offered on module_factory either: there it would let a caller that does
    /// hold a config drop it silently.
    [[nodiscard]] module_ptr create(const std::string& name) const {
        const module_factory_base& f = at(name);
        return f.create(f.make_config());
    }

    /// Creates a module of an exact version with a config at its declared defaults.
    [[nodiscard]] module_ptr create(const std::string& name, const version& v) const {
        const module_factory_base& f = at(name, v);
        return f.create(f.make_config());
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

}  // namespace atp

#endif
