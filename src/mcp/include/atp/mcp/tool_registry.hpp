// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_MCP_TOOL_REGISTRY_HPP
#define ATP_MCP_TOOL_REGISTRY_HPP

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace atp::mcp {

/// One callable the server exposes: its MCP description plus the handler behind it.
struct tool {
    std::string name;
    std::string description;
    nlohmann::json input_schema;

    /// Performs the call.
    /// @param arguments the "arguments" object of tools/call, never null
    /// @return the payload; the server serialises it into the result
    /// @throws anything derived from std::exception — the server turns it into isError
    std::function<nlohmann::json(const nlohmann::json& arguments)> run;
};

/// Owning registry of tools; tools/list is rendered straight out of it.
class tool_registry {
   public:
    /// Adds a tool. Names are expected to be unique — a duplicate would shadow the earlier entry in
    /// find(), which the tests guard against rather than the registry.
    void add(tool t) {
        tools_.push_back(std::move(t));
    }

    /// Tool by name; nullptr if there is none.
    [[nodiscard]] const tool* find(const std::string& name) const {
        for (const tool& t : tools_) {
            if (t.name == name) {
                return &t;
            }
        }
        return nullptr;
    }

    /// The "tools" array of a tools/list result, in registration order.
    [[nodiscard]] nlohmann::json describe() const {
        nlohmann::json out = nlohmann::json::array();
        for (const tool& t : tools_) {
            out.push_back({{"name", t.name}, {"description", t.description}, {"inputSchema", t.input_schema}});
        }
        return out;
    }

   private:
    std::vector<tool> tools_;
};

}  // namespace atp::mcp

#endif
