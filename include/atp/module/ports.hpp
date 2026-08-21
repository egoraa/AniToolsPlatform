// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_MODULE_PORTS_HPP
#define ANITOOLSPLATFORM_MODULE_PORTS_HPP

#include <concepts>

#include <atp/io/inputs.hpp>
#include <atp/io/outputs.hpp>
#include <atp/io/properties.hpp>

namespace atp {

/// The three section types of a module, named as one parameter of module<>.
///
///     struct my_in : io::inputs { io::input<int>& step = make<int>("step"); };
///     struct my_out : io::outputs { io::output<int>& count = make<int>("count"); };
///     using my_ports = ports<my_in, my_out>;
///     class my_module : public module<my_ports, "my"> { ... };
///
/// Unused sections are omitted from the right: ports<my_in>, ports<io::inputs, my_out>; ports<> is
/// the empty node module<> defaults to. The node is movable, following its registries: ports live on
/// the heap, so a move breaks neither the sections' reference members nor the established
/// connections — a node can be wired up before the module and handed to its constructor. Not
/// thread-safe: setup phase only.
///
/// It lives beside module<> rather than in the io layer because it is about declaring a module: the
/// io layer knows sections, not the shape a module gathers them in.
template <std::derived_from<io::inputs> TIn = io::inputs,
          std::derived_from<io::outputs> TOut = io::outputs,
          std::derived_from<io::properties> TProps = io::properties>
struct ports {
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
concept ports_list = std::derived_from<T, ports<typename T::in_type, typename T::out_type, typename T::props_type>>;

}  // namespace atp

#endif
