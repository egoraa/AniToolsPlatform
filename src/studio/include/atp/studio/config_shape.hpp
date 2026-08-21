// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_CONFIG_SHAPE_HPP
#define ATP_STUDIO_CONFIG_SHAPE_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <atp/config/fields.hpp>
#include <atp/config/node.hpp>

namespace atp::studio {

namespace detail {

/// The value a declared field shows when the document says nothing.
///
/// For a field with a default that is the default. For a **required** field it is `null`, and that is
/// the whole mechanism by which a required field can still hold the zero of its type: the view shows an
/// empty cell, anything typed into it is a value and gets written, and only the untouched cell stays
/// null and is dropped. Substituting a typed zero here instead — as an earlier version did — made `0`
/// in a required integer indistinguishable from "never filled in", so the field was thrown away and the
/// module then failed with "required and absent" on a value the person had just typed.
[[nodiscard]] inline atp::config::node default_of(const config::field_declaration& decl) {
    switch (decl.kind) {
        case config::field_kind::object:
            return atp::config::node(atp::config::node::object_type{});
        case config::field_kind::array:
            return atp::config::node(atp::config::node::array_type{});
        default:
            break;
    }
    return decl.default_value;
}

/// The value a **fresh element** of a scalar list starts at. A different question from default_of: an
/// element has no declared default of its own, and `null` in an array is not something a module can
/// read, so it starts at the zero of the element's kind.
[[nodiscard]] inline atp::config::node zero_of(config::field_kind kind) {
    switch (kind) {
        case config::field_kind::boolean:
            return false;
        case config::field_kind::integer:
            return atp::config::node(std::int64_t{0});
        case config::field_kind::real:
            return 0.0;
        default:
            break;
    }
    return std::string();
}

/// Whether a stored value has the shape its declaration calls for. A value that does not is left alone
/// by both directions: the schema says the document is wrong, and a wrong document is somebody's data
/// to fix, not the editor's to silently replace.
[[nodiscard]] inline bool shape_agrees(const config::field_declaration& decl, const atp::config::node& value) {
    switch (decl.kind) {
        case config::field_kind::object:
            return value.is_object();
        case config::field_kind::array:
            return value.is_array();
        default:
            break;
    }
    return true;
}

}  // namespace detail

/// Fills a stored config out into the object an editor shows: every declared field is present, taking
/// its default where the document said nothing.
///
/// This is what makes an editor of the **object** possible at all. A form built from the schema had to
/// enumerate fields itself and could show nothing the schema did not name; a tree of a materialised
/// object shows the declared fields and the undeclared keys alike, because by then they are the same
/// kind of thing — entries of an object.
///
/// Undeclared keys are copied through. They are not an error: a config written by hand, or by a newer
/// build of the plugin, is somebody's data and this function's business is to show it, not to judge it.
///
/// **Key order is this function's own**: the declared fields first, in the order the module declared
/// them, then whatever the document held that the schema does not name. A config::node object keeps
/// what it was given, so what an earlier version could only inherit from a std::map — alphabetical
/// order — is now a decision, and declaration order is the better one: it is the order the module's
/// author wrote, and the order a reader of the plugin's source expects. Saving is unaffected either
/// way, since json_dump sorts.
///
/// **It is a view and never a document.** What goes back is strip_defaults of whatever the editor
/// produced, so nothing materialised here can reach a saved file on its own — a property the tests pin
/// as strip(materialise(x)) == strip(x).
/// @param schema fields the module declared, empty for a module that declared none
/// @param stored the config as the document holds it
[[nodiscard]] inline atp::config::node materialise(const std::vector<config::field_declaration>& schema,
                                                   const atp::config::node& stored) {
    const atp::config::node* source = stored.is_object() ? &stored : nullptr;
    atp::config::node full(atp::config::node::object_type{});

    for (const config::field_declaration& decl : schema) {
        const atp::config::node* here = nullptr;
        if (source != nullptr) {
            const atp::config::node* found = source->find(decl.name);
            if (found != nullptr && !found->is_null()) {
                here = found;
            }
        }
        if (here != nullptr && !detail::shape_agrees(decl, *here)) {
            full[decl.name] = *here;
            continue;
        }
        if (decl.kind == config::field_kind::object) {
            full[decl.name] = materialise(
                decl.children, here != nullptr ? *here : atp::config::node(atp::config::node::object_type{}));
            continue;
        }
        if (decl.kind == config::field_kind::array) {
            atp::config::node items(atp::config::node::array_type{});
            if (here != nullptr) {
                for (const atp::config::node& item : here->elements()) {
                    items.push_back(decl.children.empty() || !item.is_object() ? item
                                                                               : materialise(decl.children, item));
                }
            }
            full[decl.name] = std::move(items);
            continue;
        }
        full[decl.name] = here != nullptr ? *here : detail::default_of(decl);
    }

    if (source != nullptr) {
        for (const auto& [key, value] : source->entries()) {
            if (full.find(key) == nullptr) {
                full[key] = value;
            }
        }
    }
    return full;
}

/// The other direction: what of an edited object is worth writing down.
///
/// A value equal to its default is dropped, and so is a group or a list that ends up with nothing to
/// say. The rule is the same one sync_persistent_properties applies to properties, and for the same
/// reason: without it, opening a module in the inspector would grow its config to the full schema, the
/// document would stop being readable and a diff would show changes nobody made.
///
/// Three things the rule does **not** apply to. A key the schema does not declare passes through
/// untouched — dropping it would delete somebody's data because this build cannot name it. So does a
/// value whose shape contradicts its declaration: the schema says the document is wrong, and saying so
/// is validation's job, not a reason for the editor to replace it. And a **required** field is always
/// written when it is there at all — it has no default to be equal to, and dropping it on the zero of
/// its type would make `0` in a required integer impossible to express.
///
/// A list element **is** thinned, down to {} when all of its fields sit at their defaults, and the
/// array keeps its length either way. That is not a loss: the position is the data, and an empty
/// element materialises back into exactly the defaults it stood for. Dropping the element itself would
/// be the loss, and this is deliberately not that — an earlier form of this code made that mistake and
/// punched holes in arrays.
/// @param schema fields the module declared
/// @param full the object an editor has been working on
[[nodiscard]] inline atp::config::node strip_defaults(const std::vector<config::field_declaration>& schema,
                                                      const atp::config::node& full) {
    if (!full.is_object()) {
        return full;
    }
    atp::config::node thin(atp::config::node::object_type{});

    for (const config::field_declaration& decl : schema) {
        const atp::config::node* found = full.find(decl.name);
        if (found == nullptr || found->is_null()) {
            continue;
        }
        if (!detail::shape_agrees(decl, *found)) {
            thin[decl.name] = *found;
            continue;
        }
        if (decl.kind == config::field_kind::object) {
            atp::config::node inner = strip_defaults(decl.children, *found);
            if (inner.size() != 0) {
                thin[decl.name] = std::move(inner);
            }
            continue;
        }
        if (decl.kind == config::field_kind::array) {
            if (found->size() == 0) {
                continue;
            }
            if (decl.children.empty()) {
                thin[decl.name] = *found;
                continue;
            }
            atp::config::node items(atp::config::node::array_type{});
            for (const atp::config::node& item : found->elements()) {
                items.push_back(item.is_object() ? strip_defaults(decl.children, item) : item);
            }
            thin[decl.name] = std::move(items);
            continue;
        }
        if (decl.required || !(*found == detail::default_of(decl))) {
            thin[decl.name] = *found;
        }
    }

    for (const auto& [key, value] : full.entries()) {
        const bool declared =
            std::ranges::any_of(schema, [&key](const config::field_declaration& d) { return d.name == key; });
        if (!declared) {
            thin[key] = value;
        }
    }
    return thin;
}

}  // namespace atp::studio

#endif
