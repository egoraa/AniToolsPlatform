// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_MCP_CONTROL_TOOLS_HPP
#define ATP_MCP_CONTROL_TOOLS_HPP

#include <concepts>
#include <exception>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <atp/io/properties.hpp>
#include <atp/mcp/arguments.hpp>
#include <atp/mcp/tool_registry.hpp>
#include <atp/mcp/type_name.hpp>
#include <atp/module/module_base.hpp>
#include <atp/runtime/connection_sample.hpp>
#include <atp/runtime/group.hpp>
#include <atp/runtime/pipeline_runner.hpp>
#include <atp/runtime/property_override.hpp>

namespace atp::mcp {

/// What the control tools need of a live pipeline. studio::session satisfies it as written, and
/// application_control gives a host that owns a runtime::application the same shape — which is the
/// point: the tools exist once, and both hosts serve an identical vocabulary.
///
/// A concept rather than an abstract base: a base would put virtuals into session for the sake of a
/// single second implementation, and nothing here is called often enough for the difference to
/// matter.
template <typename T>
concept control_target = requires(T& t, const T& ct, bool on, const runtime::property_override& o) {
    { ct.running() } -> std::same_as<bool>;
    { ct.error() } -> std::same_as<std::exception_ptr>;
    { ct.stats() } -> std::same_as<std::vector<runtime::pipeline_runner::thread_stats>>;
    { ct.live_root() } -> std::same_as<runtime::group*>;
    { ct.sample_connections() } -> std::same_as<std::vector<runtime::connection_sample>>;
    { ct.module_metrics() } -> std::same_as<std::vector<runtime::group::module_stats>>;
    { ct.input_metrics() } -> std::same_as<std::vector<runtime::group::port_stats>>;
    { ct.metrics_enabled() } -> std::same_as<bool>;
    { t.set_metrics_enabled(on) } -> std::same_as<bool>;
    t.set_property(o);
};

namespace detail {

/// Text of the runner's first error, or an empty string if the run is clean. The target hands the
/// error out as an exception_ptr, so rethrowing is the only way to read it.
[[nodiscard]] inline std::string error_text(const std::exception_ptr& error) {
    if (!error) {
        return {};
    }
    try {
        std::rethrow_exception(error);
    } catch (const std::exception& e) {
        return e.what();
    } catch (...) {
        return "unknown error";
    }
}

/// JSON name of a property kind, the same four words the catalog uses.
[[nodiscard]] inline const char* kind_name(io::property_kind kind) {
    switch (kind) {
        case io::property_kind::integer:
            return "integer";
        case io::property_kind::real:
            return "real";
        case io::property_kind::boolean:
            return "boolean";
        case io::property_kind::text:
            return "text";
    }
    return "text";
}

/// Name of an output as "child.port" within a group. The runtime keeps no such direction — a
/// connection holds raw pointers and an alias forgets the path it was made from — so the only way
/// back to a name is to scan the children's registries.
/// @return the dotted name, or an empty string if the port belongs to no child of this group
[[nodiscard]] inline std::string output_path_in(const runtime::group& g, const io::output_base* port) {
    for (const runtime::group::child& c : g.children()) {
        for (const auto& [name, p] : c.module->outputs().entries()) {
            if (p == port) {
                return c.name + "." + name;
            }
        }
    }
    return {};
}

/// Name of an input as "child.port" within a group; the mirror of output_path_in.
[[nodiscard]] inline std::string input_path_in(const runtime::group& g, const io::input_base* port) {
    for (const runtime::group::child& c : g.children()) {
        for (const auto& [name, p] : c.module->inputs().entries()) {
            if (p == port) {
                return c.name + "." + name;
            }
        }
    }
    return {};
}

/// Connections recorded by one group, both ends named.
[[nodiscard]] inline nlohmann::json connections_to_json(const runtime::group& g) {
    nlohmann::json out = nlohmann::json::array();
    for (const runtime::group::connection& c : g.connections()) {
        out.push_back({{"from", output_path_in(g, c.out)}, {"to", input_path_in(g, c.in)}});
    }
    return out;
}

/// Aliases a group publishes, each resolved back to the child port behind it.
[[nodiscard]] inline nlohmann::json expose_to_json(const runtime::group& g) {
    nlohmann::json inputs = nlohmann::json::object();
    for (const auto& [alias, p] : g.inputs().entries()) {
        const std::string target = input_path_in(g, p);
        if (!target.empty()) {
            inputs[alias] = target;
        }
    }
    nlohmann::json outputs = nlohmann::json::object();
    for (const auto& [alias, p] : g.outputs().entries()) {
        const std::string target = output_path_in(g, p);
        if (!target.empty()) {
            outputs[alias] = target;
        }
    }
    nlohmann::json out = nlohmann::json::object();
    if (!inputs.empty()) {
        out["inputs"] = std::move(inputs);
    }
    if (!outputs.empty()) {
        out["outputs"] = std::move(outputs);
    }
    return out;
}

/// Declared ports of one registry, in the order the module's author declared them.
///
/// That order is the registry's contract, so it is stable between calls and two samples stay
/// comparable. It used to be sorted by name here instead, because the registry was a hash map and
/// its order meant nothing; both halves of that reason are gone.
template <typename TRegistry>
[[nodiscard]] inline nlohmann::json ports_to_json(const TRegistry& registry) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& [name, p] : registry.entries()) {
        out.push_back({{"name", name}, {"type", type_name(p->type())}, {"thread_safe", p->thread_safe()}});
    }
    return out;
}

/// Properties of one module with the value it holds right now, which is what separates this from the
/// catalog: the catalog describes what a factory would make, this describes what is running.
[[nodiscard]] inline nlohmann::json live_properties_to_json(const io::properties& props) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& [name, p] : props.entries()) {
        out.push_back({{"name", name},
                       {"kind", kind_name(p->kind())},
                       {"value", p->to_string()},
                       {"default", p->default_string()},
                       {"persistent", p->persistent()},
                       {"options", p->options()}});
    }
    return out;
}

/// Flattens the tree into dotted paths, a parent before its children. Flat rather than nested,
/// because every other tool in this vocabulary addresses a module by its dotted path and a reader
/// should not have to reassemble one.
inline void describe_group(const runtime::group& g, const std::string& path, nlohmann::json& out) {
    for (const runtime::group::child& c : g.children()) {
        const std::string child_path = path.empty() ? c.name : path + "." + c.name;
        const module_base& m = *c.module;
        const runtime::group* sub = c.subgroup;
        nlohmann::json node{{"path", child_path},
                            {"module", std::string(m.get_name())},
                            {"version", m.get_version().to_string()},
                            {"group", sub != nullptr},
                            {"detached", c.detached},
                            {"inputs", ports_to_json(m.inputs())},
                            {"outputs", ports_to_json(m.outputs())},
                            {"properties", live_properties_to_json(m.properties())}};
        if (sub != nullptr) {
            node["connections"] = connections_to_json(*sub);
            node["expose"] = expose_to_json(*sub);
        }
        out.push_back(std::move(node));
        if (sub != nullptr) {
            describe_group(*sub, child_path, out);
        }
    }
}

}  // namespace detail

/// Registers the tools that observe and steer a live pipeline. Lifecycle is deliberately absent:
/// starting a run needs a document, and what stopping means differs between hosts, so each host
/// registers its own.
/// @param tools registry the tools are added to
/// @param target live pipeline they operate on; it must outlive the registry
template <control_target TTarget>
void register_control_tools(tool_registry& tools, TTarget& target) {
    tools.add({"get_status", "Whether a pipeline is running, its first error, and the pass counters per thread.",
               no_arguments_schema(), [&target](const nlohmann::json&) {
                   nlohmann::json threads = nlohmann::json::array();
                   for (const auto& t : target.stats()) {
                       threads.push_back({{"name", t.name}, {"passes", t.passes}, {"busy_passes", t.busy_passes}});
                   }
                   nlohmann::json status{{"running", target.running()}, {"threads", std::move(threads)}};
                   const std::string error = detail::error_text(target.error());
                   if (!error.empty()) {
                       status["error"] = error;
                   }
                   return status;
               }});

    tools.add({"describe_pipeline",
               "The live tree: every module with its dotted path, version, declared ports and the "
               "property values it holds right now. get_document shows what was written down; this "
               "shows what was built and is running, which is the only view a host without a "
               "document has.",
               no_arguments_schema(), [&target](const nlohmann::json&) {
                   nlohmann::json modules = nlohmann::json::array();
                   nlohmann::json connections = nlohmann::json::array();
                   nlohmann::json expose = nlohmann::json::object();
                   if (const runtime::group* root = target.live_root()) {
                       detail::describe_group(*root, "", modules);
                       connections = detail::connections_to_json(*root);
                       expose = detail::expose_to_json(*root);
                   }
                   return nlohmann::json{{"running", target.running()},
                                         {"modules", std::move(modules)},
                                         {"connections", std::move(connections)},
                                         {"expose", std::move(expose)}};
               }});

    tools.add({"read_connections",
               "Samples every connection: how many writes it has seen. This is how you tell a running "
               "pipeline from a merely valid one.",
               no_arguments_schema(), [&target](const nlohmann::json&) {
                   nlohmann::json connections = nlohmann::json::array();
                   for (const auto& s : target.sample_connections()) {
                       connections.push_back(
                           nlohmann::json{{"group_path", s.group_path}, {"index", s.index}, {"writes", s.writes}});
                   }
                   return nlohmann::json{{"connections", std::move(connections)}};
               }});

    tools.add({"set_module_metrics",
               "Turns per-module iterate timing on or off for the running pipeline. It is off by "
               "default because it is not free — timing every iterate cost a quarter of the "
               "throughput of a two-module pipeline — so switch it on to ask which module is slow, "
               "then off again.",
               object_schema({{"enabled", "boolean", "Whether to time each module's iterate"}}),
               [&target](const nlohmann::json& args) {
                   const nlohmann::json value = arg_scalar(args, "enabled");
                   const bool on = value.is_boolean() ? value.get<bool>() : value.dump() == "true";
                   if (!target.set_metrics_enabled(on)) {
                       throw runtime::config_error("nothing is running, so there is nothing to measure");
                   }
                   return nlohmann::json{{"enabled", on}};
               }});

    tools.add({"read_module_metrics",
               "Cost of every module of the running pipeline: how many times its iterate ran, how "
               "often it had work, and the total and worst time it took. This is what get_status "
               "cannot tell you — a thread runs an ordered list of modules, and one slow iterate "
               "among twenty looks exactly like twenty slightly slow ones. Enable it first with "
               "set_module_metrics; the counters accumulate from that moment, so two samples give "
               "you the interval between them.",
               no_arguments_schema(), [&target](const nlohmann::json&) {
                   nlohmann::json modules = nlohmann::json::array();
                   for (const auto& m : target.module_metrics()) {
                       modules.push_back({{"path", m.path},
                                          {"calls", m.calls},
                                          {"busy_calls", m.busy_calls},
                                          {"total_ns", m.total.count()},
                                          {"max_ns", m.max.count()}});
                   }
                   return nlohmann::json{{"enabled", target.metrics_enabled()}, {"modules", std::move(modules)}};
               }});

    tools.add({"read_input_metrics",
               "What every input of the running pipeline took in and what it lost: how many values "
               "arrived, how many were discarded because there was no room for them, how deep the "
               "queue is now and how deep it ever got, against the capacity the module declared. A "
               "non-zero discarded is the pipeline telling you it is losing data, which nothing "
               "else here will say. Unlike read_module_metrics this needs nothing switched on — the "
               "counters cost nothing and are always kept.",
               no_arguments_schema(), [&target](const nlohmann::json&) {
                   nlohmann::json ports = nlohmann::json::array();
                   for (const auto& p : target.input_metrics()) {
                       ports.push_back({{"path", p.path},
                                        {"received", p.stats.received},
                                        {"discarded", p.stats.discarded},
                                        {"pending", p.stats.pending},
                                        {"peak_pending", p.stats.peak_pending},
                                        {"capacity", p.stats.capacity}});
                   }
                   return nlohmann::json{{"ports", std::move(ports)}};
               }});

    tools.add({"set_live_property",
               "Edits a property of a running module on the fly. This does not touch the document — "
               "use set_property for that.",
               object_schema({{"path", "string", "Property path, as 'group.module.property'"},
                              {"value", "string", "Scalar value: number, string or boolean"}}),
               [&target](const nlohmann::json& args) {
                   const std::string path = arg_string(args, "path");
                   const nlohmann::json value = arg_scalar(args, "value");
                   const std::string text = value.is_string() ? value.get<std::string>() : value.dump();
                   target.set_property(runtime::parse_property_override(path + "=" + text));
                   return nlohmann::json{{"set", path}};
               }});
}

}  // namespace atp::mcp

#endif
