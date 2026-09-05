// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_CONFIG_NODE_HPP
#define ANITOOLSPLATFORM_CONFIG_NODE_HPP

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <atp/config/access_error.hpp>

namespace atp::config {

/// The seven forms a config value can take, in the order the C ABI's atp_config_kind spells them.
enum class kind { null, boolean, integer, real, string, array, object };

/// One node of a module's config: a scalar, an array or an object, in the seven forms above.
///
/// Deliberately a closed variant of its own and not a document library's node, because create() is
/// part of the plugin ABI and naming such a type in its signature would drag the library into every
/// plugin. Hence no parser and no serialisation here either — both live host-side.
///
/// An object keeps its entries in a vector of pairs rather than a map, so iteration is reproducible:
/// the same value always yields its keys in the same order, which is the order this value was given
/// and not necessarily the order they were written in.
///
/// Integer and real are separate forms rather than one number: a config that says 3 means a count,
/// not 3.0.
///
/// Named node rather than value because it describes one node of a tree and not the tree: what a
/// module receives is atp::raw_config, which owns the root and adds path access, the text and the
/// origin. The distinction is worth keeping in the names, since both are "the config" in conversation.
///
/// It may be edited in place — operator[](key) inserts, push_back appends, erase removes — because the
/// pipeline document and studio's config tree are nodes and both are edited by hand. What the type must
/// not name is a document library; being read-only was never the rule. There is deliberately no
/// set(key, value): it would say exactly what node[key] = value already says.
class node {
   public:
    using array_type = std::vector<node>;
    using object_type = std::vector<std::pair<std::string, node>>;

    /// The null form — distinct from an empty object, so a module may tell "nothing was said" from
    /// "nothing inside".
    node() = default;

    node(bool value) : storage_(value) {}
    node(double value) : storage_(value) {}
    node(std::string value) : storage_(std::move(value)) {}

    /// Present only to stop a string literal from becoming a boolean: a const char* converts to bool
    /// better than to std::string, so without this overload {"name", "rig"} would silently be true.
    /// Not redundant with the std::string constructor — do not remove it as such.
    node(const char* value) : storage_(std::string(value ? value : "")) {}

    /// Any integral but bool, because the literal 1 is an int and will not reach std::int64_t on its
    /// own. Character types come along with the rest, so node('x') is the integer 120 — there is no
    /// character form here.
    template <std::integral TInt>
        requires(!std::same_as<std::remove_cvref_t<TInt>, bool>)
    node(TInt value) : storage_(static_cast<std::int64_t>(value)) {}

    node(array_type items) : storage_(std::move(items)) {}
    node(object_type entries) : storage_(std::move(entries)) {}

    /// Copies out of the list, since an initializer_list only ever hands out const references. A
    /// large tree is built through the array_type/object_type constructors, which take ownership.
    [[nodiscard]] static node array(std::initializer_list<node> items) {
        return {array_type(items.begin(), items.end())};
    }

    /// Copies out of the list, for the same reason as array().
    [[nodiscard]] static node object(std::initializer_list<std::pair<std::string, node>> entries) {
        return {object_type(entries.begin(), entries.end())};
    }

    /// The form this node holds.
    ///
    /// Every mention of the enumeration inside this class is written config::kind on purpose: from
    /// here on the unqualified name means this member function, and an enumerator would not be
    /// reachable through it.
    [[nodiscard]] config::kind kind() const noexcept {
        return static_cast<config::kind>(storage_.index());
    }

    [[nodiscard]] bool is_null() const noexcept {
        return kind() == config::kind::null;
    }
    [[nodiscard]] bool is_bool() const noexcept {
        return kind() == config::kind::boolean;
    }
    [[nodiscard]] bool is_int() const noexcept {
        return kind() == config::kind::integer;
    }
    [[nodiscard]] bool is_double() const noexcept {
        return kind() == config::kind::real;
    }
    [[nodiscard]] bool is_string() const noexcept {
        return kind() == config::kind::string;
    }
    [[nodiscard]] bool is_array() const noexcept {
        return kind() == config::kind::array;
    }
    [[nodiscard]] bool is_object() const noexcept {
        return kind() == config::kind::object;
    }

    /// Length of an array or an object, 0 for every scalar form — which is what lets a traversal
    /// walk any node without asking its kind first.
    [[nodiscard]] std::size_t size() const noexcept {
        if (const array_type* items = std::get_if<array_type>(&storage_)) {
            return items->size();
        }
        if (const object_type* entries = std::get_if<object_type>(&storage_)) {
            return entries->size();
        }
        return 0;
    }

    /// The entries of an object, in the order it holds them; empty for every other form.
    ///
    /// The whole reason this class can be walked with a range-for and structured bindings instead of
    /// an index loop over key_at(i) and operator[](i), which is what every traversal in the tree used
    /// to be. A non-object answers a shared empty vector rather than throwing, so a caller may walk a
    /// node without asking its kind first — the same courtesy size() extends.
    [[nodiscard]] const object_type& entries() const noexcept {
        static const object_type none;
        const object_type* held = std::get_if<object_type>(&storage_);
        return held == nullptr ? none : *held;
    }

    /// The elements of an array, in order; empty for every other form, for the same reason.
    ///
    /// Named elements() rather than items() because nlohmann::json::items() means the **opposite** —
    /// the key/value pairs of an object — and both types are handled side by side here, twice within
    /// config_value_json.hpp alone. A false friend that compiles and silently walks nothing is worse
    /// than a longer name.
    [[nodiscard]] const array_type& elements() const noexcept {
        static const array_type none;
        const array_type* held = std::get_if<array_type>(&storage_);
        return held == nullptr ? none : *held;
    }

    /// Element of an array, or the value of an object's i-th entry.
    /// @throws access_error if this is a scalar or @p i is out of range
    [[nodiscard]] const node& operator[](std::size_t i) const {
        if (const array_type* items = std::get_if<array_type>(&storage_)) {
            if (i >= items->size()) {
                throw access_error("config: index " + std::to_string(i) + " is out of range");
            }
            return (*items)[i];
        }
        if (const object_type* entries = std::get_if<object_type>(&storage_)) {
            if (i >= entries->size()) {
                throw access_error("config: index " + std::to_string(i) + " is out of range");
            }
            return (*entries)[i].second;
        }
        throw access_error("config: not a container (found " + std::string(kind_name(kind())) + ")");
    }

    /// Element of an array, or the value of an object's i-th entry — the mutable mirror of the const
    /// overload, and it deliberately does not grow the container: an index past the end is a bug in
    /// the caller, not a request to extend.
    /// @throws access_error if this is a scalar or @p i is out of range
    [[nodiscard]] node& operator[](std::size_t i) {
        if (array_type* items = std::get_if<array_type>(&storage_)) {
            if (i >= items->size()) {
                throw access_error("config: index " + std::to_string(i) + " is out of range");
            }
            return (*items)[i];
        }
        if (object_type* entries = std::get_if<object_type>(&storage_)) {
            if (i >= entries->size()) {
                throw access_error("config: index " + std::to_string(i) + " is out of range");
            }
            return (*entries)[i].second;
        }
        throw access_error("config: not a container (found " + std::string(kind_name(kind())) + ")");
    }

    /// Appends to an array, a null node becoming one first — the array counterpart of
    /// operator[](std::string_view) and an error on every other form for the same reason.
    /// @throws access_error if this node holds a form other than null or array
    void push_back(node value) {
        if (is_null()) {
            storage_ = array_type{};
        }
        array_type* items = std::get_if<array_type>(&storage_);
        if (items == nullptr) {
            throw access_error("config: not an array (found " + std::string(kind_name(kind())) + ")");
        }
        items->push_back(std::move(value));
    }

    /// Removes the i-th element of an array or entry of an object.
    /// @return whether there was one; false for a scalar or an index past the end
    bool erase(std::size_t i) noexcept {
        if (array_type* items = std::get_if<array_type>(&storage_)) {
            if (i >= items->size()) {
                return false;
            }
            items->erase(items->begin() + static_cast<std::ptrdiff_t>(i));
            return true;
        }
        if (object_type* entries = std::get_if<object_type>(&storage_)) {
            if (i >= entries->size()) {
                return false;
            }
            entries->erase(entries->begin() + static_cast<std::ptrdiff_t>(i));
            return true;
        }
        return false;
    }

    /// The value under @p key, inserted as null at the end when the key is not there yet.
    ///
    /// A null node becomes an object first, which is what lets a builder write into a
    /// default-constructed node with no preamble. Every other form is an error rather than a silent
    /// replacement: turning a number into an object because somebody subscripted it hides a bug.
    /// @throws access_error if this node holds a form other than null or object
    [[nodiscard]] node& operator[](std::string_view key) {
        if (is_null()) {
            storage_ = object_type{};
        }
        object_type* entries = std::get_if<object_type>(&storage_);
        if (entries == nullptr) {
            throw access_error("config: not an object (found " + std::string(kind_name(kind())) + ")");
        }
        for (auto& [name, value] : *entries) {
            if (name == key) {
                return value;
            }
        }
        entries->emplace_back(std::string(key), node{});
        return entries->back().second;
    }

    /// Removes the entry under @p key.
    /// @return whether there was one; false for a node that is not an object, which is why this does
    ///         not throw — erasing what is not there is the caller's normal case
    bool erase(std::string_view key) noexcept {
        object_type* entries = std::get_if<object_type>(&storage_);
        if (entries == nullptr) {
            return false;
        }
        for (std::size_t i = 0; i < entries->size(); ++i) {
            if ((*entries)[i].first == key) {
                entries->erase(entries->begin() + static_cast<std::ptrdiff_t>(i));
                return true;
            }
        }
        return false;
    }

    /// Key of an object's i-th entry; empty for a non-object or an index out of range.
    [[nodiscard]] std::string_view key_at(std::size_t i) const noexcept {
        const object_type* entries = std::get_if<object_type>(&storage_);
        if (entries == nullptr || i >= entries->size()) {
            return {};
        }
        return (*entries)[i].first;
    }

    [[nodiscard]] const node* find(std::string_view key) const noexcept {
        const object_type* entries = std::get_if<object_type>(&storage_);
        if (entries == nullptr) {
            return nullptr;
        }
        for (const auto& [name, value] : *entries) {
            if (name == key) {
                return &value;
            }
        }
        return nullptr;
    }

    /// The stored node under @p key, or nullptr — the mutable half of the pair, for a caller editing
    /// a document in place rather than reading a module's config.
    [[nodiscard]] node* find(std::string_view key) noexcept {
        object_type* entries = std::get_if<object_type>(&storage_);
        if (entries == nullptr) {
            return nullptr;
        }
        for (auto& [name, value] : *entries) {
            if (name == key) {
                return &value;
            }
        }
        return nullptr;
    }

    /// @throws access_error naming the key that is not there
    [[nodiscard]] const node& at(std::string_view key) const {
        const node* found = find(key);
        if (found == nullptr) {
            throw access_error("config: no key '" + std::string(key) + "'");
        }
        return *found;
    }

    [[nodiscard]] std::optional<bool> try_as_bool() const noexcept {
        return try_get<bool>();
    }
    [[nodiscard]] std::optional<std::int64_t> try_as_int() const noexcept {
        return try_get<std::int64_t>();
    }
    [[nodiscard]] std::optional<double> try_as_double() const noexcept {
        return try_get<double>();
    }
    [[nodiscard]] std::optional<std::string> try_as_string() const {
        return try_get<std::string>();
    }

    /// The stored string itself, or nullptr for any other form — the one accessor that does not copy.
    ///
    /// It exists so a pointer to the string can cross the C ABI and stay valid for as long as the
    /// module lives, the tree being owned by the module's host and never modified after construction.
    /// as_string() cannot serve there: the copy it returns is gone by the time the call returns.
    [[nodiscard]] const std::string* string_ptr() const noexcept {
        return std::get_if<std::string>(&storage_);
    }

    /// @throws access_error naming the form expected and the form found
    [[nodiscard]] bool as_bool() const {
        return as<bool>(config::kind::boolean);
    }
    [[nodiscard]] std::int64_t as_int() const {
        return as<std::int64_t>(config::kind::integer);
    }
    [[nodiscard]] double as_double() const {
        return as<double>(config::kind::real);
    }
    [[nodiscard]] std::string as_string() const {
        return as<std::string>(config::kind::string);
    }

    /// Reads a key that has to be there and has to be of that form, naming both in the message when
    /// it is not — which is the whole reason these exist beside at(key).as_bool() and friends.
    ///
    /// There is deliberately no fallback overload here. Reading with a default in hand is
    /// config::bool_or and its three peers in <atp/config/read.hpp>, over a **nullable** node, so that
    /// one vocabulary serves both what node::find(key) answers and what raw_config::find(path)
    /// does. Members here would have covered only the first of the two.
    /// @throws access_error naming the key and both forms
    [[nodiscard]] bool bool_at(std::string_view key) const {
        return at_as<bool>(key, config::kind::boolean);
    }
    [[nodiscard]] std::int64_t int_at(std::string_view key) const {
        return at_as<std::int64_t>(key, config::kind::integer);
    }
    [[nodiscard]] double double_at(std::string_view key) const {
        return at_as<double>(key, config::kind::real);
    }
    [[nodiscard]] std::string string_at(std::string_view key) const {
        return at_as<std::string>(key, config::kind::string);
    }

    /// Name of a form as the error messages spell it.
    [[nodiscard]] static std::string_view kind_name(config::kind k) noexcept {
        switch (k) {
            case config::kind::null:
                return "null";
            case config::kind::boolean:
                return "boolean";
            case config::kind::integer:
                return "integer";
            case config::kind::real:
                return "real";
            case config::kind::string:
                return "string";
            case config::kind::array:
                return "array";
            case config::kind::object:
                return "object";
        }
        return "null";
    }

    /// Value equality across every form, with null distinct from an empty object or array.
    ///
    /// Object comparison is order-sensitive, because the storage is a vector and the entry order is
    /// part of what this class promises to reproduce. Two documents differing only in key order are
    /// therefore unequal here, which is why the document layer pins its round trip on the dumped text
    /// rather than on the tree.
    [[nodiscard]] bool operator==(const node&) const = default;

   private:
    template <typename T>
    [[nodiscard]] std::optional<T> try_get() const {
        if (const T* held = std::get_if<T>(&storage_)) {
            return *held;
        }
        return std::nullopt;
    }

    template <typename T>
    [[nodiscard]] T as(config::kind expected) const {
        if (const T* held = std::get_if<T>(&storage_)) {
            return *held;
        }
        throw access_error("config: not a " + std::string(kind_name(expected)) + " (found " +
                           std::string(kind_name(kind())) + ")");
    }

    template <typename T>
    [[nodiscard]] T at_as(std::string_view key, config::kind expected) const {
        const node& found = at(key);
        if (const T* held = std::get_if<T>(&found.storage_)) {
            return *held;
        }
        throw access_error("config: '" + std::string(key) + "' is not a " + std::string(kind_name(expected)) +
                           " (found " + std::string(kind_name(found.kind())) + ")");
    }

    /// The one implicit mapping in this class, made explicit: kind() answers the variant's index, so
    /// the enumeration and the alternative order are one contract. c_module.hpp spells the same seven
    /// forms out as a switch precisely so that a reordering of the C header fails to compile here;
    /// these do the same for a reordering of the alternatives, which would otherwise silently rename
    /// every form and leave that switch faithfully translating the wrong answer.
    ///
    /// The marker below answers a false positive rather than a finding: clang-tidy 23 reads the call
    /// in the template argument as a C-style cast and offers to wrap it in a static_cast, which is
    /// exactly what to_underlying is there to avoid spelling.
    using storage_type = std::variant<std::monostate, bool, std::int64_t, double, std::string, array_type, object_type>;

    template <config::kind K, typename T>
    static constexpr bool alternative_is =
        // NOLINTNEXTLINE(modernize-avoid-c-style-cast)
        std::same_as<std::variant_alternative_t<std::to_underlying(K), storage_type>, T>;

    static_assert(alternative_is<config::kind::null, std::monostate>);
    static_assert(alternative_is<config::kind::boolean, bool>);
    static_assert(alternative_is<config::kind::integer, std::int64_t>);
    static_assert(alternative_is<config::kind::real, double>);
    static_assert(alternative_is<config::kind::string, std::string>);
    static_assert(alternative_is<config::kind::array, array_type>);
    static_assert(alternative_is<config::kind::object, object_type>);
    static_assert(std::variant_size_v<storage_type> == 7);

    storage_type storage_;
};

}  // namespace atp::config

#endif
