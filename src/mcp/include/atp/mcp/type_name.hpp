#ifndef ATP_MCP_TYPE_NAME_HPP
#define ATP_MCP_TYPE_NAME_HPP

#include <string>
#include <typeindex>

namespace atp::mcp {

/// Name of a port or value type as the agent sees it. The single point where this is decided: the
/// string is readable on MSVC and mangled on libstdc++, and the studio already prints it raw in its
/// type mismatch message (port_types.hpp), so this matches the GUI until a demangler is added here.
[[nodiscard]] inline std::string type_name(std::type_index type) {
    return type.name();
}

}  // namespace atp::mcp

#endif
