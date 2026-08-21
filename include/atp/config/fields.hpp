// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_CONFIG_FIELDS_HPP
#define ANITOOLSPLATFORM_CONFIG_FIELDS_HPP

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <atp/config/node.hpp>
#include <atp/config/read.hpp>
#include <atp/module/module_config.hpp>

namespace atp::config {

/// What a declared field holds. The four scalars are node's own forms; object and array are the two
/// shapes group() and list() produce.
enum class field_kind { boolean, integer, real, string, object, array };

/// One declared field, as a host sees it without a module.
struct field_declaration {
    std::string name;
    field_kind kind = field_kind::string;

    /// Value used when the key is absent; null for a required field and for object/array.
    node default_value;

    /// Whether the field was declared without a default, so its absence is a problem.
    bool required = false;

    /// Fields of a nested object, or of the element type of an array of objects. Empty otherwise.
    std::vector<field_declaration> children;

    /// For an array of scalars, what one element is. Meaningless for every other kind, and for an
    /// array of objects, where `children` says it instead.
    ///
    /// It has to be recorded rather than inferred: a schema is read without a module, so nothing else
    /// remembers whether `tags` holds strings or numbers, and an editor drawing a row for an element
    /// has no other way to know what to parse it as.
    field_kind element = field_kind::string;
};

/// Ordered registry of the fields a module declares its config out of.
///
/// An heir declares them as reference members, exactly as an io section declares ports:
///
///     struct scaler_config : atp::config::fields {
///         using fields::fields;
///         double& gain = field("gain", 1.0);
///         std::string& device = field<std::string>("device");
///     };
///
/// The `using fields::fields;` is not decoration: group() and list() build an heir from a node and an
/// origin, and an heir that hid those constructors could not be nested in another config.
///
/// The base subobject is constructed before the members, so by the time a field() call runs in a
/// member-initializer the source is already in place — the same language guarantee the port sections
/// stand on.
///
/// **Nothing here ever throws on bad data.** An unknown key is one no declaration claimed, and that is
/// only knowable once every declaration has run — which is after the last member-initializer of the
/// heir, where neither this class nor an heir using `using fields::fields;` has any code of its own.
/// Problems are therefore collected, and throwing is the job of whoever knows the object is complete:
/// module_factory::create calls throw_if_invalid() before it builds the module. A module built by hand
/// can call it too.
///
/// Neither copyable nor movable, and it cannot become either: an heir binds references into this
/// object's own storage. That is also why every container here is a deque — a vector would rehome its
/// elements on growth and leave those references dangling.
///
/// Not thread-safe and not meant to be: a config is read once, in a constructor.
class fields {
   public:
    /// Declares a schema with no config behind it: every field takes its default and nothing is
    /// missing, because nothing was asked for. This is how a factory reads a module's schema without a
    /// module, and it is deliberately different from a config that is present and empty — there a
    /// required field really is absent.
    fields() = default;

    /// @param source root of the config, an object or null
    /// @param origin file the config came from, named in messages; empty when it came from none
    explicit fields(node source, std::string origin = {})
        : source_(std::move(source)), origin_(std::move(origin)), has_source_(true) {}

    /// The form a module's constructor actually has in hand.
    explicit fields(const module_config& cfg) : fields(cfg.root(), cfg.origin()) {}

    fields(const fields&) = delete;
    fields& operator=(const fields&) = delete;
    virtual ~fields() = default;

    /// Declared fields, in declaration order.
    [[nodiscard]] const std::vector<field_declaration>& declared() const noexcept {
        return declared_;
    }

    /// Everything wrong with the config: a wrong type or a missing required field where it was found,
    /// then the keys nobody claimed. Each reads "path: what is wrong". Empty when the config is good.
    [[nodiscard]] const std::vector<std::string>& problems() const {
        collect_unknown();
        return problems_;
    }

    /// @throws config::access_error listing every problem at once, naming the file when there is one
    void throw_if_invalid() const {
        const std::vector<std::string>& found = problems();
        if (found.empty()) {
            return;
        }
        std::string text = origin_.empty() ? std::string("config: ") : "config " + origin_ + ": ";
        text += std::to_string(found.size());
        text += found.size() == 1 ? " problem" : " problems";
        for (const std::string& p : found) {
            text += "\n  ";
            text += p;
        }
        throw access_error(text);
    }

   protected:
    /// Declares an optional scalar field: the value under @p name, or @p fallback when absent.
    /// @param name key within this object
    /// @param fallback value used when the key is absent
    template <scalar T>
    T& field(std::string name, T fallback) {
        return declare_scalar<T>(std::move(name), std::move(fallback), false);
    }

    /// Declares an optional string field written with a literal.
    ///
    /// Present because a literal is a const char*, which is not one of the four scalar forms: without
    /// it the ordinary spelling field("preset", "default") would not compile at all. Deliberately an
    /// overload rather than a wider concept — the concept is what the four storages are keyed by, and
    /// widening it would ask for a fifth that does not exist. The same reason node carries its own
    /// const char* constructor; do not remove either as redundant.
    /// @param name key within this object
    /// @param fallback value used when the key is absent
    std::string& field(std::string name, const char* fallback) {
        return declare_scalar<std::string>(std::move(name), std::string(fallback == nullptr ? "" : fallback), false);
    }

    /// Declares a required scalar field. The type has to be spelled out because there is no default to
    /// deduce it from — the same shape as make<std::string>("file", "") in an io section.
    /// @param name key within this object
    template <scalar T>
    T& field(std::string name) {
        return declare_scalar<T>(std::move(name), T{}, true);
    }

    /// Declares a nested object.
    ///
    /// An absent key is not a problem in itself: the group is built from an empty object, so its own
    /// optional fields take their defaults while its own required fields are still reported missing. A
    /// key holding something that is not an object is one problem and not a cascade — the group is then
    /// built with no source at all, which keeps its fields quiet.
    /// @param name key within this object
    template <std::derived_from<fields> T>
    T& group(std::string name) {
        declare_nested<T>(name, field_kind::object);
        std::shared_ptr<T> child = build_group<T>(name);
        T& ref = *child;
        owned_.push_back(std::move(child));
        return ref;
    }

    /// Declares an array of nested objects — one element per item, each read from its own node.
    /// @param name key within this object
    template <std::derived_from<fields> T>
    std::deque<T>& list(std::string name) {
        declare_nested<T>(name, field_kind::array);
        auto items = std::make_shared<std::deque<T>>();
        fill_group_list(name, *items);
        std::deque<T>& ref = *items;
        owned_.push_back(std::move(items));
        return ref;
    }

    /// Declares an array of scalars.
    /// @param name key within this object
    template <scalar T>
    std::deque<T>& list(std::string name) {
        field_declaration decl;
        decl.name = name;
        decl.kind = field_kind::array;
        decl.element = kind_of<T>();
        declared_.push_back(std::move(decl));

        auto items = std::make_shared<std::deque<T>>();
        fill_scalar_list(name, *items);
        std::deque<T>& ref = *items;
        owned_.push_back(std::move(items));
        return ref;
    }

   private:
    template <scalar T>
    T& declare_scalar(std::string name, T fallback, bool required) {
        field_declaration decl;
        decl.name = name;
        decl.kind = kind_of<T>();
        decl.required = required;
        if (!required) {
            decl.default_value = node(fallback);
        }
        declared_.push_back(std::move(decl));

        T& slot = store<T>(std::move(fallback));
        if (!has_source_) {
            return slot;
        }
        const node* found = source_.find(name);
        if (found == nullptr || found->is_null()) {
            if (required) {
                problems_.push_back(name + ": required and absent");
            }
            return slot;
        }
        if (std::optional<T> read = read_as<T>(*found)) {
            slot = std::move(*read);
        } else {
            problems_.push_back(mismatch(name, scalar_name<T>(), *found));
        }
        return slot;
    }

    template <std::derived_from<fields> T>
    void declare_nested(const std::string& name, field_kind kind) {
        field_declaration decl;
        decl.name = name;
        decl.kind = kind;
        const T probe;
        decl.children = probe.declared();
        declared_.push_back(std::move(decl));
    }

    template <std::derived_from<fields> T>
    [[nodiscard]] std::shared_ptr<T> build_group(const std::string& name) {
        if (!has_source_) {
            return std::make_shared<T>();
        }
        const node* found = source_.find(name);
        if (found == nullptr || found->is_null()) {
            std::shared_ptr<T> child = std::make_shared<T>(node(node::object_type{}), origin_);
            adopt(*child, name + ".");
            return child;
        }
        if (!found->is_object()) {
            problems_.push_back(mismatch(name, "object", *found));
            return std::make_shared<T>();
        }
        std::shared_ptr<T> child = std::make_shared<T>(*found, origin_);
        adopt(*child, name + ".");
        return child;
    }

    template <std::derived_from<fields> T>
    void fill_group_list(const std::string& name, std::deque<T>& items) {
        const node* found = array_source(name);
        if (found == nullptr) {
            return;
        }
        for (std::size_t i = 0; i < found->size(); ++i) {
            const node& item = (*found)[i];
            const std::string at = name + "[" + std::to_string(i) + "]";
            if (!item.is_object()) {
                problems_.push_back(mismatch(at, "object", item));
                items.emplace_back();
                continue;
            }
            items.emplace_back(item, origin_);
            adopt(items.back(), at + ".");
        }
    }

    template <scalar T>
    void fill_scalar_list(const std::string& name, std::deque<T>& items) {
        const node* found = array_source(name);
        if (found == nullptr) {
            return;
        }
        for (std::size_t i = 0; i < found->size(); ++i) {
            const node& item = (*found)[i];
            if (std::optional<T> read = read_as<T>(item)) {
                items.push_back(std::move(*read));
            } else {
                problems_.push_back(mismatch(name + "[" + std::to_string(i) + "]", scalar_name<T>(), item));
            }
        }
    }

    /// The array under @p name, or nullptr when there is nothing to read — recording the mismatch when
    /// the key holds something that is not an array.
    [[nodiscard]] const node* array_source(const std::string& name) {
        if (!has_source_) {
            return nullptr;
        }
        const node* found = source_.find(name);
        if (found == nullptr || found->is_null()) {
            return nullptr;
        }
        if (!found->is_array()) {
            problems_.push_back(mismatch(name, "array", *found));
            return nullptr;
        }
        return found;
    }

    /// Moves a nested config's problems up, putting this level's path in front of each. The child knows
    /// nothing about where it sits and should not — the prefix belongs to whoever placed it.
    void adopt(const fields& child, const std::string& prefix) {
        for (const std::string& p : child.problems()) {
            problems_.push_back(prefix + p);
        }
    }

    [[nodiscard]] static std::string mismatch(const std::string& path, std::string_view expected, const node& found) {
        return path + ": expected " + std::string(expected) + ", found " + std::string(node::kind_name(found.kind()));
    }

    /// Reads one node as T, widening a whole number into a real and nothing else.
    ///
    /// The widening is not a convenience: a JSON writer emits 48000 for a real, node keeps integers and
    /// reals apart, and without this a real field would silently take its default on a perfectly
    /// ordinary config. The other direction stays refused — a real in an integer field is a problem
    /// even without a fraction, because the document spells the two differently and a round trip has to
    /// keep them apart.
    template <scalar T>
    [[nodiscard]] static std::optional<T> read_as(const node& n) {
        if constexpr (std::same_as<T, double>) {
            if (std::optional<std::int64_t> whole = n.try_as_int()) {
                return static_cast<double>(*whole);
            }
            return n.try_as_double();
        } else if constexpr (std::same_as<T, bool>) {
            return n.try_as_bool();
        } else if constexpr (std::same_as<T, std::int64_t>) {
            return n.try_as_int();
        } else {
            return n.try_as_string();
        }
    }

    template <scalar T>
    [[nodiscard]] static constexpr field_kind kind_of() {
        if constexpr (std::same_as<T, bool>) {
            return field_kind::boolean;
        } else if constexpr (std::same_as<T, std::int64_t>) {
            return field_kind::integer;
        } else if constexpr (std::same_as<T, double>) {
            return field_kind::real;
        } else {
            return field_kind::string;
        }
    }

    /// Name of the **expected** form as a message spells it. Deliberately not node::kind_name, which
    /// names the form that was found; the two stand in one sentence and must not be one function.
    template <scalar T>
    [[nodiscard]] static constexpr std::string_view scalar_name() {
        if constexpr (std::same_as<T, bool>) {
            return "boolean";
        } else if constexpr (std::same_as<T, std::int64_t>) {
            return "integer";
        } else if constexpr (std::same_as<T, double>) {
            return "real";
        } else {
            return "string";
        }
    }

    template <scalar T>
    T& store(T value) {
        if constexpr (std::same_as<T, bool>) {
            bools_.push_back(value);
            return bools_.back();
        } else if constexpr (std::same_as<T, std::int64_t>) {
            ints_.push_back(value);
            return ints_.back();
        } else if constexpr (std::same_as<T, double>) {
            reals_.push_back(value);
            return reals_.back();
        } else {
            strings_.push_back(std::move(value));
            return strings_.back();
        }
    }

    void collect_unknown() const {
        if (unknown_collected_) {
            return;
        }
        unknown_collected_ = true;
        if (!has_source_ || !source_.is_object()) {
            return;
        }
        for (const auto& entry : source_.entries()) {
            const std::string& key = entry.first;
            const bool claimed =
                std::ranges::any_of(declared_, [&key](const field_declaration& d) { return d.name == key; });
            if (!claimed) {
                problems_.push_back(key + ": not a field of this config");
            }
        }
    }

    node source_;
    std::string origin_;
    bool has_source_ = false;
    std::vector<field_declaration> declared_;
    mutable std::vector<std::string> problems_;
    mutable bool unknown_collected_ = false;

    std::deque<bool> bools_;
    std::deque<std::int64_t> ints_;
    std::deque<double> reals_;
    std::deque<std::string> strings_;

    /// Nested configs and lists, kept alive with their type erased: group() and list() are templates,
    /// so their storage cannot be a member of a fixed type. shared_ptr<void> carries the right deleter
    /// on its own, which a unique_ptr<void> would need spelled out by hand.
    std::vector<std::shared_ptr<void>> owned_;
};

}  // namespace atp::config

#endif
