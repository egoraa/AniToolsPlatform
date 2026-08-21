// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_MODULE_SERVICE_DIRECTORY_HPP
#define ANITOOLSPLATFORM_MODULE_SERVICE_DIRECTORY_HPP

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>

#include <atp/support/type_compare.hpp>

namespace atp {

/// Non-owning directory of services: which provider (by name) offers which interfaces. A module
/// publishes its interfaces in initialize() and peers look them up in start(), so the publish and
/// lookup phases make module initialisation order irrelevant. The key is the pair (name, interface
/// type): one provider may publish several interfaces, and one interface may be published under
/// several names.
///
/// Type safety without dynamic_cast: the pointer is stored as void* but can only be retrieved under
/// the same static type it was stored with, the match being guarded by type_index, which makes the
/// reverse static_cast correct by construction — the same discipline as
/// input_base::accepts()/deliver().
///
/// Lifetime is the caller's contract, as with io connections: publications are removed before the
/// service is destroyed, normally in stop(). Not thread-safe — a setup-phase entity like the io
/// registries. That covers the directory alone: peers call a discovered interface from their own
/// threads, so in a multi-threaded pipeline the thread safety of the PUBLISHED interface is its
/// author's business.
class service_directory {
   public:
    service_directory() = default;

    service_directory(const service_directory&) = delete;
    service_directory& operator=(const service_directory&) = delete;

    /// Publishes an interface under a provider name. TService is spelled out explicitly
    /// (provide<camera_control>(...)): deducing it from the argument would substitute the concrete
    /// module class instead of the interface, and no consumer would find the entry. A const type is
    /// rejected, since typeid strips const and provide<const T> would silently collide with
    /// provide<T>.
    /// @throws std::invalid_argument on an empty name
    /// @throws std::runtime_error if this provider already published this interface
    template <typename TService>
        requires(!std::is_const_v<TService>)
    void provide(const std::string& name, TService& service) {
        if (name.empty()) {
            throw std::invalid_argument("empty service provider name");
        }
        auto& services = entries_[name];
        auto [it, inserted] = services.try_emplace(std::type_index(typeid(TService)), std::addressof(service));
        if (!inserted) {
            throw std::runtime_error("duplicate service '" + name + "' interface '" + typeid(TService).name() + "'");
        }
    }

    /// Looks an interface up by provider name.
    /// @throws std::runtime_error if there is no such provider, or it publishes no such interface —
    ///         the two cases carry different messages
    template <typename TService>
    [[nodiscard]] TService& at(const std::string& name) const {
        auto it = entries_.find(name);
        if (it == entries_.end()) {
            throw std::runtime_error("no service provider named '" + name + "'");
        }
        auto found = it->second.find(std::type_index(typeid(TService)));
        if (found == it->second.end()) {
            throw std::runtime_error("provider '" + name + "' has no interface '" + typeid(TService).name() + "'");
        }
        return *static_cast<TService*>(found->second);
    }

    /// Looks an interface up by provider name; nullptr if the provider or the interface is missing.
    template <typename TService>
    [[nodiscard]] TService* find(const std::string& name) const {
        auto it = entries_.find(name);
        if (it == entries_.end()) {
            return nullptr;
        }
        auto found = it->second.find(std::type_index(typeid(TService)));
        return found == it->second.end() ? nullptr : static_cast<TService*>(found->second);
    }

    /// Removes every publication of a name — the usual thing to do in stop().
    /// @return false if the name published nothing
    bool remove(const std::string& name) {
        return entries_.erase(name) > 0;
    }

    /// Removes a single publication. An entry left without interfaces is erased whole, keeping the
    /// "inner map is never empty" invariant.
    /// @return false if there was no such publication
    template <typename TService>
    bool remove(const std::string& name) {
        auto it = entries_.find(name);
        if (it == entries_.end()) {
            return false;
        }
        if (it->second.erase(std::type_index(typeid(TService))) == 0) {
            return false;
        }
        if (it->second.empty()) {
            entries_.erase(it);
        }
        return true;
    }

   private:
    std::unordered_map<std::string, std::map<std::type_index, void*, type_name_less>> entries_;
};

}  // namespace atp

#endif
