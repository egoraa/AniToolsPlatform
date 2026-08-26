// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_RUNTIME_CONFIG_BINDING_HPP
#define ATP_RUNTIME_CONFIG_BINDING_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <atp/config/node.hpp>
#include <atp/config/scalar.hpp>
#include <atp/module/module_config.hpp>
#include <atp/runtime/config_error.hpp>
#include <atp/runtime/config_source.hpp>
#include <atp/runtime/raw_config.hpp>

namespace atp::runtime {

namespace detail {

/// The one line every problem below is spelled with: "path: expected X, found Y".
[[nodiscard]] inline std::string config_binding_mismatch(const std::string& path,
                                                         std::string_view expected,
                                                         const atp::config::node& found) {
    return path + ": expected " + std::string(expected) + ", found " +
           std::string(atp::config::node::kind_name(found.kind()));
}

/// The other line a declared value can be refused with: the form was right and the value was not one
/// the field accepts. It lists the whole set, because a reader who does not know the names has no way
/// to guess them from the document.
[[nodiscard]] inline std::string config_binding_not_allowed(const std::string& path,
                                                            const std::string& text,
                                                            const std::vector<std::string>& options) {
    std::string wanted;
    for (const std::string& option : options) {
        if (!wanted.empty()) {
            wanted += '|';
        }
        wanted += option;
    }
    return path + ": '" + text + "' is not one of " + wanted;
}

[[nodiscard]] constexpr std::string_view field_kind_name(atp::field_kind kind) noexcept {
    switch (kind) {
        case atp::field_kind::boolean:
            return "boolean";
        case atp::field_kind::integer:
            return "integer";
        case atp::field_kind::real:
            return "real";
        case atp::field_kind::string:
            return "string";
        case atp::field_kind::object:
            return "object";
        case atp::field_kind::array:
            return "array";
    }
    return "unknown";
}

/// Reads one node as T, widening a whole number into a real and never the other way around: a JSON
/// writer emits 48000 for a real, config::node keeps integers and reals apart, and without the
/// widening a real field would silently keep its default on an entirely ordinary config. The reverse
/// stays refused even without a fraction, because the document spells the two forms differently and a
/// round trip through save_fields has to keep them apart.
template <atp::config::scalar T>
[[nodiscard]] std::optional<T> read_scalar(const atp::config::node& n) {
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

/// An absent or null key is never a problem here: it is the caller (load_scalar_field for a required
/// field, load_group_field and load_list_field never) that decides whether that silence is allowed.
template <atp::config::scalar T>
void load_scalar_field(atp::module_config::entry& e,
                       const atp::config::node* found,
                       const std::string& path,
                       std::vector<std::string>& problems) {
    if (found == nullptr || found->is_null()) {
        if (e.required()) {
            problems.push_back(path + ": required and absent");
        }
        return;
    }
    if (std::optional<T> value = read_scalar<T>(*found)) {
        e.set(std::move(*value));
    } else {
        problems.push_back(config_binding_mismatch(path, field_kind_name(e.kind()), *found));
    }
}

/// A string field and an enumeration are read through the same door, and it is not set().
///
/// entry::from_string is the only call that both parses the name and checks it against the declared
/// set; for a plain string it is the identity, so nothing is lost by routing both through it. Reading
/// an enumeration with set(std::string) would not merely be wrong, it would **throw**: the field holds
/// a value of its own type, not a name, and load_fields owes its caller a list of problems rather than
/// an exception.
inline void load_text_field(atp::module_config::entry& e,
                            const atp::config::node* found,
                            const std::string& path,
                            std::vector<std::string>& problems) {
    if (found == nullptr || found->is_null()) {
        if (e.required()) {
            problems.push_back(path + ": required and absent");
        }
        return;
    }
    const std::optional<std::string> text = found->try_as_string();
    if (!text) {
        problems.push_back(config_binding_mismatch(path, "string", *found));
        return;
    }
    if (!e.from_string(*text)) {
        problems.push_back(config_binding_not_allowed(path, *text, e.options()));
    }
}

void load_fields_into(atp::module_config& target,
                      const atp::config::node& doc,
                      const std::string& prefix,
                      std::vector<std::string>& problems);

/// A group is optional on its own: an absent or null key leaves every field inside it at its declared
/// default, so there is nothing to recurse into. A key that holds something other than an object is
/// one problem and no cascade — the nested fields are then not read at all, since a "found" they could
/// react to does not exist.
inline void load_group_field(atp::module_config::entry& e,
                             const atp::config::node* found,
                             const std::string& path,
                             std::vector<std::string>& problems) {
    if (found == nullptr || found->is_null()) {
        return;
    }
    if (!found->is_object()) {
        problems.push_back(config_binding_mismatch(path, "object", *found));
        return;
    }
    load_fields_into(e.group(), *found, path + ".", problems);
}

/// One element of an array of scalars; unlike a group's fields it is written directly through
/// entry::values() rather than through set(), since there is no per-element entry to mark set — the
/// list's own is_set(), raised by resize() below, already says the array was deliberately sized.
template <atp::config::scalar T>
void load_list_element(atp::module_config::entry& e,
                       std::size_t i,
                       const atp::config::node& item,
                       const std::string& at,
                       std::vector<std::string>& problems) {
    if (std::optional<T> value = read_scalar<T>(item)) {
        e.values<T>()[i] = std::move(*value);
    } else {
        problems.push_back(config_binding_mismatch(at, field_kind_name(e.element()), item));
    }
}

/// One element of an array of strings or of enumerations, written through set_element_from_string for
/// the reason load_text_field gives: it is the only call that knows the element's own type and the
/// declared set. Unlike the typed elements beside it this does raise the list's is_set(), which resize()
/// has already raised anyway.
inline void load_text_element(atp::module_config::entry& e,
                              std::size_t i,
                              const atp::config::node& item,
                              const std::string& at,
                              std::vector<std::string>& problems) {
    const std::optional<std::string> text = item.try_as_string();
    if (!text) {
        problems.push_back(config_binding_mismatch(at, "string", item));
        return;
    }
    if (!e.set_element_from_string(i, *text)) {
        problems.push_back(config_binding_not_allowed(at, *text, e.options()));
    }
}

/// A list is read only from an array in the document, one problem otherwise; its length is aligned to
/// the document's through entry::resize(), and a bad element is one problem naming its index rather
/// than a reason to stop — the position is still meaningful data even when the value at it is not.
inline void load_list_field(atp::module_config::entry& e,
                            const atp::config::node* found,
                            const std::string& path,
                            std::vector<std::string>& problems) {
    if (found == nullptr || found->is_null()) {
        return;
    }
    if (!found->is_array()) {
        problems.push_back(config_binding_mismatch(path, "array", *found));
        return;
    }
    e.resize(found->size());
    for (std::size_t i = 0; i < found->size(); ++i) {
        const atp::config::node& item = (*found)[i];
        const std::string at = path + "[" + std::to_string(i) + "]";
        if (e.element() == atp::field_kind::object) {
            if (!item.is_object()) {
                problems.push_back(config_binding_mismatch(at, "object", item));
                continue;
            }
            load_fields_into(e.group_at(i), item, at + ".", problems);
            continue;
        }
        switch (e.element()) {
            case atp::field_kind::boolean:
                load_list_element<bool>(e, i, item, at, problems);
                break;
            case atp::field_kind::integer:
                load_list_element<std::int64_t>(e, i, item, at, problems);
                break;
            case atp::field_kind::real:
                load_list_element<double>(e, i, item, at, problems);
                break;
            case atp::field_kind::string:
                load_text_element(e, i, item, at, problems);
                break;
            case atp::field_kind::object:
            case atp::field_kind::array:
                break;
        }
    }
}

/// Walks one config's own entries, then the document's own keys the other way round. The two directions
/// cannot be one loop: a declared field with nothing in the document is not a problem, while a document
/// key claimed by no field is. The path prefix is always what the caller of this function already
/// placed — a nested config does not know, and must not need to know, where it sits.
inline void load_fields_into(atp::module_config& target,
                             const atp::config::node& doc,
                             const std::string& prefix,
                             std::vector<std::string>& problems) {
    for (atp::module_config::entry& e : target.entries()) {
        const atp::config::node* found = doc.find(e.name());
        const std::string path = prefix + std::string(e.name());
        switch (e.kind()) {
            case atp::field_kind::boolean:
                load_scalar_field<bool>(e, found, path, problems);
                break;
            case atp::field_kind::integer:
                load_scalar_field<std::int64_t>(e, found, path, problems);
                break;
            case atp::field_kind::real:
                load_scalar_field<double>(e, found, path, problems);
                break;
            case atp::field_kind::string:
                load_text_field(e, found, path, problems);
                break;
            case atp::field_kind::object:
                load_group_field(e, found, path, problems);
                break;
            case atp::field_kind::array:
                load_list_field(e, found, path, problems);
                break;
        }
    }
    for (const auto& doc_entry : doc.entries()) {
        if (target.find(doc_entry.first) == nullptr) {
            problems.push_back(prefix + doc_entry.first + ": not a field of this config");
        }
    }
}

/// The string kind is saved through to_string() rather than value<std::string>(), because that kind
/// covers an enumeration too and an enumeration does not hold a string — to_string() is the one call
/// that prints whatever the field actually holds, in the canonical form the set was checked against.
[[nodiscard]] inline atp::config::node save_scalar_field(const atp::module_config::entry& e) {
    switch (e.kind()) {
        case atp::field_kind::boolean:
            return atp::config::node(e.value<bool>());
        case atp::field_kind::integer:
            return atp::config::node(e.value<std::int64_t>());
        case atp::field_kind::real:
            return atp::config::node(e.value<double>());
        case atp::field_kind::string:
            return atp::config::node(e.to_string());
        case atp::field_kind::object:
        case atp::field_kind::array:
            break;
    }
    return atp::config::node();
}

[[nodiscard]] atp::config::node save_fields_of(const atp::module_config& source);

[[nodiscard]] inline atp::config::node save_list_element(const atp::module_config::entry& e, std::size_t i) {
    if (e.element() == atp::field_kind::object) {
        return save_fields_of(e.group_at(i));
    }
    switch (e.element()) {
        case atp::field_kind::boolean:
            return atp::config::node(e.values<bool>()[i]);
        case atp::field_kind::integer:
            return atp::config::node(e.values<std::int64_t>()[i]);
        case atp::field_kind::real:
            return atp::config::node(e.values<double>()[i]);
        case atp::field_kind::string:
            return atp::config::node(e.element_string(i));
        case atp::field_kind::object:
        case atp::field_kind::array:
            break;
    }
    return atp::config::node();
}

/// What of one config is worth writing down, in its own declaration order.
///
/// A scalar goes by is_set() and is_default(): required or not, a field nobody wrote never appears,
/// and an optional field written back to exactly its default is dropped rather than saved as noise. A
/// group entry's own is_set() never becomes true — module_config raises it only on the entries inside
/// a group, never on the group field itself — so whether to write the group is decided from what its
/// own save_fields_of() has to say, not from a flag that could never answer. A list is the opposite:
/// its is_set() is exactly what resize() raises, so it alone says whether the array was deliberately
/// sized; once it is, every element is written, thinned to {} at its own defaults, because dropping an
/// element rather than emptying it would shift every index after it.
inline atp::config::node save_fields_of(const atp::module_config& source) {
    atp::config::node result(atp::config::node::object_type{});
    for (const atp::module_config::entry& e : source.entries()) {
        switch (e.kind()) {
            case atp::field_kind::boolean:
            case atp::field_kind::integer:
            case atp::field_kind::real:
            case atp::field_kind::string:
                if (e.is_set() && (e.required() || !e.is_default())) {
                    result[e.name()] = save_scalar_field(e);
                }
                break;
            case atp::field_kind::object: {
                atp::config::node nested = save_fields_of(e.group());
                if (!nested.entries().empty()) {
                    result[e.name()] = std::move(nested);
                }
                break;
            }
            case atp::field_kind::array: {
                if (!e.is_set()) {
                    break;
                }
                atp::config::node items(atp::config::node::array_type{});
                for (std::size_t i = 0; i < e.size(); ++i) {
                    items.push_back(save_list_element(e, i));
                }
                result[e.name()] = std::move(items);
                break;
            }
        }
    }
    return result;
}

[[nodiscard]] atp::config::node values_of_config(const atp::module_config& source);

[[nodiscard]] inline atp::config::node values_of_element(const atp::module_config::entry& e, std::size_t i) {
    if (e.element() == atp::field_kind::object) {
        return values_of_config(e.group_at(i));
    }
    return save_list_element(e, i);
}

inline atp::config::node values_of_config(const atp::module_config& source) {
    atp::config::node result(atp::config::node::object_type{});
    for (const atp::module_config::entry& e : source.entries()) {
        switch (e.kind()) {
            case atp::field_kind::boolean:
            case atp::field_kind::integer:
            case atp::field_kind::real:
            case atp::field_kind::string:
                result[e.name()] = save_scalar_field(e);
                break;
            case atp::field_kind::object:
                result[e.name()] = values_of_config(e.group());
                break;
            case atp::field_kind::array: {
                atp::config::node items(atp::config::node::array_type{});
                for (std::size_t i = 0; i < e.size(); ++i) {
                    items.push_back(values_of_element(e, i));
                }
                result[e.name()] = std::move(items);
                break;
            }
        }
    }
    return result;
}

}  // namespace detail

/// Fills a declared config with the values of a document. Never throws for bad data: every problem is
/// one line "path: what is wrong", and the caller decides what to do with the list.
///
/// The target must be **fresh** — an object straight from module_factory_base::make_config(). Fields
/// the document says nothing about keep what they were built with, so loading twice into one object
/// would leave the first document's values behind.
///
/// The source is attached whatever happens, so a module handed a file of a format the host does not
/// parse still finds its bytes in text() with no field filled.
///
/// A raw_config is the one target that takes the document over whole instead of being walked: it
/// declares no field, so it has nothing to fill and nothing to call an unknown key — there is no
/// declaration for a key to be unknown to. Recognising it here rather than through a virtual on the
/// base keeps the knowledge where both sides are host-side, and leaves the SDK unaware that a config
/// carrying a tree exists at all.
[[nodiscard]] inline std::vector<std::string> load_fields(atp::module_config& target, const config_source& source) {
    if (auto* raw = dynamic_cast<raw_config*>(&target)) {
        raw->adopt(source);
        return {};
    }
    std::vector<std::string> problems;
    detail::load_fields_into(target, source.root, "", problems);
    target.attach_source(source.text, source.origin, source.opaque);
    return problems;
}

/// @throws config_error listing every problem at once, naming the file when the source had one
inline void load_fields_or_throw(atp::module_config& target, const config_source& source) {
    const std::vector<std::string> problems = load_fields(target, source);
    if (problems.empty()) {
        return;
    }
    std::string text = source.origin.empty() ? std::string("config: ") : "config " + source.origin + ": ";
    text += std::to_string(problems.size());
    text += problems.size() == 1 ? " problem" : " problems";
    for (const std::string& p : problems) {
        text += "\n  ";
        text += p;
    }
    throw config_error(text);
}

/// The other direction: what of a filled config is worth writing down.
///
/// A scalar is written when somebody wrote it and it is either required or different from its
/// default. A group is written when it has anything to say; a list keeps its length and its elements
/// are thinned the same way, down to {} — the position is the data, and dropping the element itself
/// would punch a hole in the array.
/// See values_of for the opposite question, which a module of the C path asks.
[[nodiscard]] inline atp::config::node save_fields(const atp::module_config& source) {
    return detail::save_fields_of(source);
}

/// Every declared field of @p source as a tree, defaults included — what a module of the C path reads.
///
/// The deliberate opposite of save_fields, which drops a field nobody wrote and one written back to its
/// own default because a saved project is worth no noise. A module is worth the opposite: a key it
/// declared and did not write is a key at its default, not a key that is absent, and a script reading
/// its config as a dictionary would otherwise still need a fallback for every declared key — which is
/// exactly what declaring them was meant to end. **Do not fold the two together.**
///
/// A required field nobody filled writes the zero of its form. That is reachable only through the
/// module_registry::create overloads that fill nothing; a pipeline throws before it ever gets here.
[[nodiscard]] inline atp::config::node values_of(const atp::module_config& source) {
    return detail::values_of_config(source);
}

}  // namespace atp::runtime

#endif
