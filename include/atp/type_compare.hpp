// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_TYPE_COMPARE_HPP
#define ANITOOLSPLATFORM_TYPE_COMPARE_HPP

#include <cstring>
#include <typeindex>
#include <typeinfo>

namespace atp {

/// Whether two type tags denote the same type, decided by name rather than by identity.
///
/// The platform loads modules from shared libraries, so the two tags handed to a comparison are
/// routinely produced in different binaries: an output's `typeid(T)` is evaluated where the output
/// was instantiated, the input's where the input was. `std::type_index` equality forwards to
/// `std::type_info::operator==`, and what that does is implementation-defined — libstdc++ and the
/// MSVC STL fall back to comparing the name strings, while Apple's libc++ compares only the address,
/// assuming the dynamic linker merged every typeinfo into one object. Plugins are built with hidden
/// visibility, which is precisely what prevents that merging: the typeinfo of a port payload is
/// emitted into the plugin and never exported. Comparing names makes the answer the same everywhere.
///
/// Two caveats come with the rule. Both binaries must be built from this header — a plugin compiled
/// against an older SDK keeps the address comparison inside its own `accepts()`. And the type has to
/// have external linkage, which the platform already requires of port and service types: a leading
/// '*' marks a typeinfo the Itanium ABI does not guarantee to be unique by name, and for those the
/// address stays the only valid answer.
///
/// The linkage requirement is a genuine narrowing on MSVC, where name() is the undecorated name while
/// the STL's own operator== compares the decorated one: two distinct types of internal linkage
/// sharing an identifier would be told apart by `==` and not by this function. MSVC marks no such
/// typeinfo the way the Itanium ABI does, so the guard above cannot catch that case. It is accepted
/// deliberately — a payload or interface type of internal linkage cannot be named by two binaries at
/// once, so it is outside the contract to begin with.
[[nodiscard]] inline bool same_type(std::type_index left, std::type_index right) noexcept {
    if (left == right) {
        return true;
    }
    if (*left.name() == '*' || *right.name() == '*') {
        return false;
    }
    return std::strcmp(left.name(), right.name()) == 0;
}

/// Ordering counterpart of same_type, for containers keyed by a type tag: `std::less<type_index>`
/// orders by `type_info::before()`, which is address-based on the same platforms and for the same
/// reason. Types of internal linkage are outside the contract here, as they are for same_type.
struct type_name_less {
    [[nodiscard]] bool operator()(std::type_index left, std::type_index right) const noexcept {
        return std::strcmp(left.name(), right.name()) < 0;
    }
};

}  // namespace atp

#endif
