// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_IO_IO_REGISTRY_HPP
#define ANITOOLSPLATFORM_IO_IO_REGISTRY_HPP

#include <concepts>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>
#include <vector>

#include <atp/io/io_base.hpp>
#include <atp/io/threading.hpp>
#include <atp/support/type_compare.hpp>

namespace atp::io::detail {

/// Shared machinery of the input, output and property registries; owns its entries.
///
/// An heir declares entries as reference members: `input<int>& number = make<input<int>>("number")`.
/// Non-copyable, since those references are bound to concrete objects inside the registry, and not
/// thread-safe — make/remove/get belong to the setup phase.
///
/// **Enumeration is in declaration order, and that is a contract rather than an observation.**
/// list(), entries() and owned() hand entries back in the order the author wrote them, so the MCP
/// description, the studio inspector and the ports drawn on a canvas node all show one order — the
/// one from the source. It used to be a hash map, and each of those three surfaces either sorted by
/// name to compensate or showed hash order; the sorts are gone with the map.
///
/// Hence a vector and a linear search. It is **not** the faster structure and the trade is paid, not
/// avoided: measured in Release on this tree, one find() over four ports costs 10.7 ns against the
/// hash map's 8.3 ns, and the gap widens with size (16 ports: 25 vs 8, 64 ports: 93 vs 11). What
/// makes that the right price is where lookups happen — connecting a pipeline and answering a
/// description, both setup or request time — while iterate() addresses ports through the heir's own
/// references and never searches at all. A module with enough ports for the gap to matter does not
/// exist; if one ever does, the answer is an index beside the vector, not a return to hash order.
///
/// Entry objects move as the vector grows, but the ports themselves never do — they live behind
/// unique_ptr, which is what keeps addresses taken before a section is moved still valid afterwards.
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
        if (find_entry(name) != nullptr) {
            throw std::runtime_error("duplicate " + std::string(kind_) + " name '" + name + "'");
        }
        auto item = std::make_unique<TItem>(name, std::forward<TArgs>(args)...);
        TItem& ref = *item;
        registry_.push_back({std::move(name), std::move(item), &ref});
        return ref;
    }

    /// Publishes someone else's port under a name of this registry, without taking ownership.
    /// Lifetime is the caller's contract: the alias must not outlive the port (in a composite
    /// group this holds structurally — it owns its children).
    /// @throws std::runtime_error if the name is already taken
    template <std::derived_from<TBase> TItem>
    TItem& alias(std::string name, TItem& port) {
        if (find_entry(name) != nullptr) {
            throw std::runtime_error("duplicate " + std::string(kind_) + " name '" + name + "'");
        }
        registry_.push_back({std::move(name), nullptr, &port});
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
        const entry* e = find_entry(name);
        return e == nullptr ? nullptr : e->port;
    }

    /// Drops the entry with this name, leaving the order of the rest as it was.
    /// @return false if there was no such entry
    bool remove(const std::string& name) {
        return std::erase_if(registry_, [&name](const entry& e) { return e.name == name; }) > 0;
    }

    /// Every entry, owned and aliased alike, in declaration order.
    [[nodiscard]] std::vector<const TBase*> list() const {
        std::vector<const TBase*> result;
        result.reserve(registry_.size());
        for (const entry& e : registry_) {
            result.push_back(e.port);
        }
        return result;
    }

    /// Every entry paired with the name it is registered under.
    ///
    /// Not the same as walking list() and asking each port for its name: an alias publishes someone
    /// else's port under a name of this registry, and the port object keeps its own. A path is
    /// written in terms of the registry's name, so anything that has to produce a path — the
    /// description of a live tree, above all — has to read it from here.
    [[nodiscard]] std::vector<std::pair<std::string, TBase*>> entries() const {
        std::vector<std::pair<std::string, TBase*>> result;
        result.reserve(registry_.size());
        for (const entry& e : registry_) {
            result.emplace_back(e.name, e.port);
        }
        return result;
    }

    /// Owned entries only — the material for the runner's port-to-thread map, from which group
    /// registries drop out on their own, holding nothing but aliases.
    [[nodiscard]] std::vector<TBase*> owned() const {
        std::vector<TBase*> result;
        for (const entry& e : registry_) {
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
        std::string name;
        std::unique_ptr<TBase> owned;
        TBase* port = nullptr;
    };

    [[nodiscard]] const entry* find_entry(std::string_view name) const {
        for (const entry& e : registry_) {
            if (e.name == name) {
                return &e;
            }
        }
        return nullptr;
    }

    std::string_view kind_;
    std::vector<entry> registry_;
};

}  // namespace atp::io::detail

#endif
