// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_MODULE_DYNAMIC_CONFIG_HPP
#define ANITOOLSPLATFORM_MODULE_DYNAMIC_CONFIG_HPP

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <atp/module/module_config.hpp>

namespace atp {

/// A config whose shape is data rather than a type: what a host builds when it has to describe a module
/// it did not compile — a plugin of the C path, a script behind a bridge.
///
/// module_config declares through field/group/list, each of which takes the form of the field from a
/// C++ type. There is no such type here, so this class declares from a field_kind and a default in
/// canonical string form. The entries it produces are indistinguishable from declared ones, which is
/// the whole point: load_fields, save_fields, the studio config tree and the MCP catalog walk both
/// without asking who declared them.
///
/// It holds no state of its own — every declaration lands in the base's storage.
class dynamic_config : public module_config {
   public:
    /// Declares an optional scalar.
    /// @param name key within this object
    /// @param kind one of the four scalar forms
    /// @param fallback the default, in the same canonical form a property default is written in
    /// @param options canonical strings it accepts — a non-empty set makes it an enumeration
    /// @throws config::access_error if @p kind is not a scalar form, if @p fallback does not parse as
    ///         @p kind, or if it is outside @p options
    void scalar(std::string name, field_kind kind, std::string fallback, std::vector<std::string> options = {}) {
        declare({std::move(name), kind, field_kind::string, std::move(fallback), std::move(options), nullptr});
    }

    /// Declares a required scalar — one with no default, whose absence from a document is a problem
    /// rather than a fallback.
    ///
    /// Spelled as its own call rather than as an absent default, because "required means no default" is
    /// the rule and a call site passing an empty optional would hide it.
    /// @param name key within this object
    /// @param kind one of the four scalar forms
    /// @param options canonical strings it accepts — a non-empty set makes it an enumeration
    /// @throws config::access_error if @p kind is not a scalar form
    void required_scalar(std::string name, field_kind kind, std::vector<std::string> options = {}) {
        declare({std::move(name), kind, field_kind::string, std::nullopt, std::move(options), nullptr});
    }

    /// Declares an array of scalars, empty until somebody grows it.
    ///
    /// The options are listed here rather than taken from the element type, which is the only way to
    /// declare an array of enumerations for a form that has no name table — and no form reaching this
    /// class has one.
    /// @param name key within this object
    /// @param element one of the four scalar forms; an array of objects is object_list's business
    /// @param options canonical strings every element accepts
    /// @throws config::access_error if @p element is not a scalar form
    void scalar_list(std::string name, field_kind element, std::vector<std::string> options = {}) {
        declare({std::move(name), field_kind::array, element, std::nullopt, std::move(options), nullptr});
    }

    /// Declares a nested object, empty until it is declared into.
    /// @param name key within this object
    /// @return the child, to go on declaring in
    dynamic_config& object(std::string name) {
        return group<dynamic_config>(std::move(name));
    }

    /// Declares an array of nested objects, empty until somebody grows it.
    ///
    /// @p declare_element is replayed rather than stored as a built object: it runs once now to make the
    /// prototype element_shape() answers, and again for every element the array grows by. That is what
    /// makes "what an element would be" and "what an element is" the same answer by construction.
    /// @param name key within this object
    /// @param declare_element declares the fields of one element into a fresh child
    void object_list(std::string name, std::function<void(dynamic_config&)> declare_element) {
        declare({std::move(name),
                 field_kind::array,
                 field_kind::object,
                 std::nullopt,
                 {},
                 [make = std::move(declare_element)] {
                     auto child = std::make_shared<dynamic_config>();
                     make(*child);
                     return std::static_pointer_cast<module_config>(child);
                 }});
    }
};

}  // namespace atp

#endif
