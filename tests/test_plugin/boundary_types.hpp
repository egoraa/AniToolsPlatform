#ifndef ANITOOLSPLATFORM_TESTS_BOUNDARY_TYPES_HPP
#define ANITOOLSPLATFORM_TESTS_BOUNDARY_TYPES_HPP

namespace atp_tests {

/// @file
/// Vocabulary shared by the host and the ports test plugin: a port payload and a service interface.
/// Both are user-defined types with external linkage, deliberately not in an anonymous namespace.
/// The typeinfo of a fundamental type such as int lives in the C++ runtime and is therefore one
/// object for the whole process, which would hide the very mismatch these types exist to expose: the
/// typeinfo of a type like these is emitted into every library that needs it, and a plugin built with
/// hidden visibility does not export its copy.

/// Payload travelling over a connection between a host module and a plugin module.
struct boundary_payload {
    int value = 0;
};

/// Interface the plugin's module publishes in the service directory for a host peer to find.
struct boundary_service {
    boundary_service() = default;
    boundary_service(const boundary_service&) = delete;
    boundary_service& operator=(const boundary_service&) = delete;
    virtual ~boundary_service() = default;

    [[nodiscard]] virtual int doubled(int value) const = 0;
};

}  // namespace atp_tests

#endif
