// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_CONFIG_VALUE_HPP
#define ANITOOLSPLATFORM_CONFIG_VALUE_HPP

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace atp {

/// The seven forms a config value can take, in the order the C ABI's atp_config_kind spells them.
enum class config_kind { null, boolean, integer, real, string, array, object };

/// Reading a config went wrong: a key is missing, or the value found is of another form.
///
/// The message names the key whenever the call that failed knew it — int_at("rate") can say which
/// key disappointed it, as_int() on a value already in hand can only name the form it found. A full
/// path through the tree ("channels[2].rate") is deliberately absent: it would require every value
/// to know its parent, which means owning references inside a value type, and the module reads its
/// config in the constructor where the two levels of access above are enough to say what happened.
class bad_config : public std::runtime_error {
   public:
    using std::runtime_error::runtime_error;
};

/// A structured setting handed to a module at creation — the channel for what a property cannot
/// express: a list, a table, a nested object, needed before initialize and not edited live.
///
/// Deliberately its own closed variant rather than nlohmann::json, because create() is part of the
/// plugin ABI and naming that type in its signature would drag the library into every plugin — the
/// same reason atp_runtime is not exported. Conversion from JSON lives host-side, in
/// atp::runtime::to_config_value.
///
/// An object keeps its entries in a vector of pairs rather than a map, so iteration is reproducible:
/// the same value always yields the same handles in the C path's flat index and the same insertion
/// order into a bridge's dictionary — the reason the Lua package's port table is an ordered proxy
/// too. What this does NOT preserve is the order written in the file: a document is read as
/// nlohmann::json, whose object is a std::map, so keys arrive already sorted. The order kept here is
/// the order this value was given.
///
/// Integer and real are separate forms, by the same argument that makes the config model store JSON
/// nodes instead of strings and keep 5 apart from "5": a config that says 3 means a count, not 3.0,
/// and the platform's own port types already distinguish i64 from f64.
class config_value {
   public:
    using array_type = std::vector<config_value>;
    using object_type = std::vector<std::pair<std::string, config_value>>;

    /// The null form, which is also what a module gets when its node carries no config at all —
    /// distinct from an empty object, so a module may tell "nothing was said" from "nothing inside".
    config_value() = default;

    config_value(bool value) : storage_(value) {}
    config_value(double value) : storage_(value) {}
    config_value(std::string value) : storage_(std::move(value)) {}

    /// Present only to stop a string literal from becoming a boolean: a const char* converts to bool
    /// better than to std::string, so without this overload {"name", "rig"} would silently be true.
    /// It is not redundant with the std::string constructor — do not remove it as such.
    config_value(const char* value) : storage_(std::string(value ? value : "")) {}

    /// Any integral but bool, because the literal 1 is an int and will not reach std::int64_t on its
    /// own. Character types come along with the rest, so config_value('x') is the integer 120 —
    /// there is no character form here, and this says so rather than leaving it to be discovered.
    template <std::integral TInt>
        requires(!std::same_as<std::remove_cvref_t<TInt>, bool>)
    config_value(TInt value) : storage_(static_cast<std::int64_t>(value)) {}

    config_value(array_type items) : storage_(std::move(items)) {}
    config_value(object_type entries) : storage_(std::move(entries)) {}

    /// Copies out of the list, since an initializer_list only ever hands out const references. A
    /// large tree is built through the array_type/object_type constructors, which take ownership.
    [[nodiscard]] static config_value array(std::initializer_list<config_value> items) {
        return config_value(array_type(items.begin(), items.end()));
    }

    /// Copies out of the list, for the same reason as array().
    [[nodiscard]] static config_value object(std::initializer_list<std::pair<std::string, config_value>> entries) {
        return config_value(object_type(entries.begin(), entries.end()));
    }

    [[nodiscard]] config_kind kind() const noexcept {
        return static_cast<config_kind>(storage_.index());
    }

    [[nodiscard]] bool is_null() const noexcept {
        return kind() == config_kind::null;
    }
    [[nodiscard]] bool is_bool() const noexcept {
        return kind() == config_kind::boolean;
    }
    [[nodiscard]] bool is_int() const noexcept {
        return kind() == config_kind::integer;
    }
    [[nodiscard]] bool is_double() const noexcept {
        return kind() == config_kind::real;
    }
    [[nodiscard]] bool is_string() const noexcept {
        return kind() == config_kind::string;
    }
    [[nodiscard]] bool is_array() const noexcept {
        return kind() == config_kind::array;
    }
    [[nodiscard]] bool is_object() const noexcept {
        return kind() == config_kind::object;
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

    /// Element of an array, or the value of an object's i-th entry.
    /// @throws bad_config if this is a scalar or @p i is out of range
    [[nodiscard]] const config_value& operator[](std::size_t i) const {
        if (const array_type* items = std::get_if<array_type>(&storage_)) {
            if (i >= items->size()) {
                throw bad_config("config: index " + std::to_string(i) + " is out of range");
            }
            return (*items)[i];
        }
        if (const object_type* entries = std::get_if<object_type>(&storage_)) {
            if (i >= entries->size()) {
                throw bad_config("config: index " + std::to_string(i) + " is out of range");
            }
            return (*entries)[i].second;
        }
        throw bad_config("config: not a container (found " + std::string(kind_name(kind())) + ")");
    }

    /// Key of an object's i-th entry; empty for a non-object or an index out of range.
    [[nodiscard]] std::string_view key_at(std::size_t i) const noexcept {
        const object_type* entries = std::get_if<object_type>(&storage_);
        if (entries == nullptr || i >= entries->size()) {
            return {};
        }
        return (*entries)[i].first;
    }

    [[nodiscard]] const config_value* find(std::string_view key) const noexcept {
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

    /// @throws bad_config naming the key that is not there
    [[nodiscard]] const config_value& at(std::string_view key) const {
        const config_value* found = find(key);
        if (found == nullptr) {
            throw bad_config("config: no key '" + std::string(key) + "'");
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
    /// It exists for the C path: a foreign module is handed a pointer to this string and told it stays
    /// valid for as long as the module lives, which is true because the tree is owned by the module's
    /// host object and never modified after construction. as_string() cannot serve there, since the
    /// copy it returns would be gone by the time the callback returns.
    [[nodiscard]] const std::string* string_ptr() const noexcept {
        return std::get_if<std::string>(&storage_);
    }

    /// @throws bad_config naming the form expected and the form found
    [[nodiscard]] bool as_bool() const {
        return as<bool>(config_kind::boolean);
    }
    [[nodiscard]] std::int64_t as_int() const {
        return as<std::int64_t>(config_kind::integer);
    }
    [[nodiscard]] double as_double() const {
        return as<double>(config_kind::real);
    }
    [[nodiscard]] std::string as_string() const {
        return as<std::string>(config_kind::string);
    }

    /// @throws bad_config naming the key and both forms
    [[nodiscard]] bool bool_at(std::string_view key) const {
        return at_as<bool>(key, config_kind::boolean);
    }
    [[nodiscard]] std::int64_t int_at(std::string_view key) const {
        return at_as<std::int64_t>(key, config_kind::integer);
    }
    [[nodiscard]] double double_at(std::string_view key) const {
        return at_as<double>(key, config_kind::real);
    }
    [[nodiscard]] std::string string_at(std::string_view key) const {
        return at_as<std::string>(key, config_kind::string);
    }

    /// Answers @p fallback when the key is absent OR holds another form — the two failures a caller
    /// with a default in hand treats the same way.
    [[nodiscard]] bool bool_at(std::string_view key, bool fallback) const {
        return value(key, fallback);
    }
    [[nodiscard]] std::int64_t int_at(std::string_view key, std::int64_t fallback) const {
        return value(key, fallback);
    }
    [[nodiscard]] double double_at(std::string_view key, double fallback) const {
        return value(key, fallback);
    }
    [[nodiscard]] std::string string_at(std::string_view key, std::string fallback) const {
        return value(key, std::move(fallback));
    }

    /// The *_at(key, fallback) overloads generalised over the four scalar types.
    template <typename T>
    [[nodiscard]] T value(std::string_view key, T fallback) const {
        const config_value* found = find(key);
        if (found == nullptr) {
            return fallback;
        }
        if (const T* held = std::get_if<T>(&found->storage_)) {
            return *held;
        }
        return fallback;
    }

    /// Name of a form as the error messages spell it.
    [[nodiscard]] static std::string_view kind_name(config_kind k) noexcept {
        switch (k) {
            case config_kind::null:
                return "null";
            case config_kind::boolean:
                return "boolean";
            case config_kind::integer:
                return "integer";
            case config_kind::real:
                return "real";
            case config_kind::string:
                return "string";
            case config_kind::array:
                return "array";
            case config_kind::object:
                return "object";
        }
        return "null";
    }

   private:
    template <typename T>
    [[nodiscard]] std::optional<T> try_get() const {
        if (const T* held = std::get_if<T>(&storage_)) {
            return *held;
        }
        return std::nullopt;
    }

    template <typename T>
    [[nodiscard]] T as(config_kind expected) const {
        if (const T* held = std::get_if<T>(&storage_)) {
            return *held;
        }
        throw bad_config("config: not a " + std::string(kind_name(expected)) + " (found " +
                         std::string(kind_name(kind())) + ")");
    }

    template <typename T>
    [[nodiscard]] T at_as(std::string_view key, config_kind expected) const {
        const config_value& found = at(key);
        if (const T* held = std::get_if<T>(&found.storage_)) {
            return *held;
        }
        throw bad_config("config: '" + std::string(key) + "' is not a " + std::string(kind_name(expected)) +
                         " (found " + std::string(kind_name(found.kind())) + ")");
    }

    std::variant<std::monostate, bool, std::int64_t, double, std::string, array_type, object_type> storage_;
};

}  // namespace atp

#endif
