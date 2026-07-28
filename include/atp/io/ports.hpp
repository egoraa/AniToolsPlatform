#ifndef ANITOOLSPLATFORM_IO_PORTS_HPP
#define ANITOOLSPLATFORM_IO_PORTS_HPP

#include <concepts>

#include <atp/io/inputs.hpp>
#include <atp/io/outputs.hpp>
#include <atp/io/properties.hpp>

namespace atp::io {

/// A module's port node: the three sections "inputs + outputs + properties", passed to module<> as
/// a single parameter. The sections are ordinary inputs/outputs/properties heirs declared in the
/// usual way; the node merely gathers them:
///
///     struct my_in : inputs { input<int>& step = make<input<int>>("step"); };
///     struct my_out : outputs { output<int>& count = make<output<int>>("count"); };
///     struct my_props : properties { property<int>& limit = make<property<int>>("limit", 10); };
///     using my_ports = ports<my_in, my_out, my_props>;
///     class my_module : public module<my_ports, "my"> { ... };
///
/// Unneeded sections are omitted from the right: ports<my_in> / ports<inputs, my_out> /
/// ports<my_in, my_out>; ports<> is the empty node module<> defaults to. The node is movable
/// (following its registries): ports live on the heap, so a move breaks neither the sections'
/// reference members nor the established connections — a node can be wired up before the module
/// and handed to its constructor. Not thread-safe: setup phase only.
template <std::derived_from<inputs> TIn = inputs,
          std::derived_from<outputs> TOut = outputs,
          std::derived_from<properties> TProps = properties>
struct ports {
    // Section types feed the covariant inputs()/outputs()/properties() of module<>.
    using in_type = TIn;
    using out_type = TOut;
    using props_type = TProps;

    TIn in;
    TOut out;
    TProps props;
};

/// A port node accepted by module<>: `ports` itself or an heir of it. Stating the requirement
/// through in_type/out_type/props_type lets substitution reject types without those members, while
/// the constraints of `ports` reject sections not derived from inputs/outputs/properties.
template <typename T>
concept ports_node = std::derived_from<T, ports<typename T::in_type, typename T::out_type, typename T::props_type>>;

}  // namespace atp::io

#endif  // ANITOOLSPLATFORM_IO_PORTS_HPP
