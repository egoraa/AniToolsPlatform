#ifndef ANITOOLSPLATFORM_IO_IO_REGISTRY_HPP
#define ANITOOLSPLATFORM_IO_IO_REGISTRY_HPP

#include <concepts>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

#include <atp/io/io_base.hpp>
#include <atp/io/threading.hpp>
#include <atp/type_compare.hpp>

namespace atp::io::detail {

/// Shared machinery of the input, output and property registries; owns its entries.
///
/// An heir declares entries as reference members: `input<int>& number = make<input<int>>("number")`.
/// Non-copyable, since those references are bound to concrete objects inside the registry, and not
/// thread-safe — make/remove/get belong to the setup phase.
/// @tparam TBase type-erased base of the entries stored here
template <std::derived_from<io_base> TBase>
class io_registry {
   public:
    io_registry(const io_registry&) = delete;
    io_registry& operator=(const io_registry&) = delete;

    /// Creates and stores an entry. The argument tail is forwarded to the entry's constructor as
    /// is: an input gets (name, safety), a property (name, default, tags).
    /// @throws std::runtime_error if the name is already taken
    template <std::derived_from<TBase> TItem, typename... TArgs>
    TItem& make(std::string name, TArgs&&... args) {
        auto item = std::make_unique<TItem>(name, std::forward<TArgs>(args)...);
        TItem& ref = *item;
        auto [it, inserted] = registry_.try_emplace(std::move(name));
        if (!inserted) {
            throw std::runtime_error("duplicate " + std::string(kind_) + " name '" + ref.name() + "'");
        }
        it->second = {std::move(item), &ref};
        return ref;
    }

    /// Publishes someone else's port under a name of this registry, without taking ownership.
    /// Lifetime is the caller's contract: the alias must not outlive the port (in a composite
    /// group this holds structurally — it owns its children).
    /// @throws std::runtime_error if the name is already taken
    template <std::derived_from<TBase> TItem>
    TItem& alias(std::string name, TItem& port) {
        auto [it, inserted] = registry_.try_emplace(std::move(name));
        if (!inserted) {
            throw std::runtime_error("duplicate " + std::string(kind_) + " name '" + it->first + "'");
        }
        it->second = {nullptr, &port};
        return port;
    }

    /// Looks an entry up by name, requiring an exact dynamic type match.
    /// @throws std::runtime_error if there is no such entry or its kind differs
    template <std::derived_from<TBase> TItem>
    [[nodiscard]] TItem& get(const std::string& name) {
        TBase& base = at(name);
        if (!same_type(typeid(base), typeid(TItem))) {
            throw std::runtime_error(std::string(kind_) + " '" + name + "' has a different type");
        }
        return static_cast<TItem&>(base);
    }

    /// Type-erased lookup by name.
    /// @throws std::runtime_error if there is no such entry
    [[nodiscard]] TBase& at(const std::string& name) const {
        TBase* item = find(name);
        if (!item) {
            throw std::runtime_error("no " + std::string(kind_) + " named '" + name + "'");
        }
        return *item;
    }

    /// Type-erased lookup by name; nullptr if there is no such entry. Const, yet hands out a
    /// mutable reference: entries are held by pointer, so the registry's constness does not extend
    /// to the ports.
    [[nodiscard]] TBase* find(const std::string& name) const {
        auto it = registry_.find(name);
        return it == registry_.end() ? nullptr : it->second.port;
    }

    /// Drops the entry with this name.
    /// @return false if there was no such entry
    bool remove(const std::string& name) {
        return registry_.erase(name) > 0;
    }

    /// Every entry, owned and aliased alike.
    [[nodiscard]] std::vector<const TBase*> list() const {
        std::vector<const TBase*> result;
        result.reserve(registry_.size());
        for (const auto& [name, e] : registry_) {
            result.push_back(e.port);
        }
        return result;
    }

    /// Owned entries only — the material for the runner's port-to-thread map, from which group
    /// registries drop out on their own, holding nothing but aliases.
    [[nodiscard]] std::vector<TBase*> owned() const {
        std::vector<TBase*> result;
        for (const auto& [name, e] : registry_) {
            if (e.owned) {
                result.push_back(e.port);
            }
        }
        return result;
    }

   protected:
    /// @param kind word for error messages ("input"/"output"); a string literal is expected, so it
    ///        is stored as a non-owning view
    explicit io_registry(std::string_view kind) : kind_(kind) {}

    io_registry(io_registry&&) = default;
    ~io_registry() = default;

   private:
    struct entry {
        std::unique_ptr<TBase> owned;
        TBase* port = nullptr;
    };

    std::string_view kind_;
    std::unordered_map<std::string, entry> registry_;
};

}  // namespace atp::io::detail

#endif
