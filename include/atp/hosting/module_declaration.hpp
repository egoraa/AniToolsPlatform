// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_HOSTING_MODULE_DECLARATION_HPP
#define ANITOOLSPLATFORM_HOSTING_MODULE_DECLARATION_HPP

#include <concepts>
#include <string>
#include <typeindex>
#include <vector>

#include <atp/io/input_base.hpp>
#include <atp/io/inputs.hpp>
#include <atp/io/output_base.hpp>
#include <atp/io/outputs.hpp>
#include <atp/io/properties.hpp>
#include <atp/io/property_base.hpp>
#include <atp/io/property_codec.hpp>
#include <atp/module/module_base.hpp>
#include <atp/module/ports.hpp>

namespace atp {

/// One declared port: the name it is registered under and the type it carries.
struct port_declaration {
    std::string name;
    std::type_index type;
};

/// One declared property: everything a host needs to draw an editor for it and to turn what was
/// typed back into a config value. The option set is copied rather than referenced, so a declaration
/// outlives whatever it was read from.
struct property_declaration {
    std::string name;
    io::property_kind kind;
    std::string default_value;
    std::vector<std::string> options;
    bool persistent = true;
};

/// What a module declares, without a module. Carries no name and no version: a factory answers those
/// itself, and one module type may be registered under several names.
///
/// A type of its own rather than three loose vectors, which is what leaves room for a
/// declaration(const module_config&) one day. It does **not** carry the declared config: a config is
/// an object the factory hands out through make_config(), and describing one by copying it into a
/// second representation was exactly the duplication that cost this project three implementations of
/// the same rules.
struct module_declaration {
    std::vector<port_declaration> inputs;
    std::vector<port_declaration> outputs;
    std::vector<property_declaration> properties;
};

/// Reads three sections into a declaration, in declaration order.
///
/// The one place that walks the registries: both the static path (a port node built from the type)
/// and the probe path (a module that was created after all) go through it, so a field added to
/// property_declaration cannot reach one path and miss the other.
/// @param in inputs section
/// @param out outputs section
/// @param props properties section
[[nodiscard]] inline module_declaration declare_from_sections(const io::inputs& in,
                                                              const io::outputs& out,
                                                              const io::properties& props) {
    module_declaration decl;
    for (const io::input_base* p : in.owned()) {
        decl.inputs.push_back({p->name(), p->type()});
    }
    for (const io::output_base* p : out.owned()) {
        decl.outputs.push_back({p->name(), p->type()});
    }
    for (const io::property_base* p : props.owned()) {
        decl.properties.push_back({p->name(), p->kind(), p->default_string(), p->options(), p->persistent()});
    }
    return decl;
}

/// Contract "this module names its port node": a ports_type typedef that can be default-built.
///
/// Both halves matter. Without ports_list the typedef could be anything; without
/// default_initializable a node whose sections take constructor arguments would fail deep inside the
/// walk instead of at the concept, and the factory would have no way to fall back.
template <typename T>
concept declares_ports = requires { typename T::ports_type; } && ports_list<typename T::ports_type> &&
                         std::default_initializable<typename T::ports_type>;

/// Describes a module type by building its port node alone — the module is never constructed.
///
/// Legitimate because a section's ports are declared by its member initializers, so a default-built
/// node holds exactly what an instance would. It stops being true for a module that declares ports in
/// its own constructor body; no module written against module<> does, and one that did would have to
/// answer through a factory of its own, the way the host's adapter for foreign-language modules does.
template <ports_list TPorts>
    requires std::default_initializable<TPorts>
[[nodiscard]] module_declaration declare_from_ports() {
    const TPorts node;
    return declare_from_sections(node.in, node.out, node.props);
}

/// Describes an existing module through its type-erased sections — the fallback for a module written
/// by hand from module_base, which names no node.
/// @param m module to read
[[nodiscard]] inline module_declaration declare_from_module(const module_base& m) {
    return declare_from_sections(m.inputs(), m.outputs(), m.properties());
}

}  // namespace atp

#endif
