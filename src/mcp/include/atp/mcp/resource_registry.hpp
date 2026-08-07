// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_MCP_RESOURCE_REGISTRY_HPP
#define ATP_MCP_RESOURCE_REGISTRY_HPP

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace atp::mcp {

/// One readable resource: its MCP description plus the reader behind it.
struct resource {
    std::string uri;
    std::string name;
    std::string mime_type;

    /// Produces the current contents.
    /// @return the text body handed to the client
    /// @throws anything derived from std::exception — the server turns it into a protocol error
    std::function<std::string()> read;
};

/// Owning registry of resources; resources/list is rendered straight out of it.
class resource_registry {
   public:
    /// Adds a resource. Uris are expected to be unique, the same way tool names are.
    void add(resource r) {
        resources_.push_back(std::move(r));
    }

    /// Resource by uri; nullptr if there is none.
    [[nodiscard]] const resource* find(const std::string& uri) const {
        for (const resource& r : resources_) {
            if (r.uri == uri) {
                return &r;
            }
        }
        return nullptr;
    }

    /// The "resources" array of a resources/list result, in registration order.
    [[nodiscard]] nlohmann::json describe() const {
        nlohmann::json out = nlohmann::json::array();
        for (const resource& r : resources_) {
            out.push_back({{"uri", r.uri}, {"name", r.name}, {"mimeType", r.mime_type}});
        }
        return out;
    }

   private:
    std::vector<resource> resources_;
};

}  // namespace atp::mcp

#endif
