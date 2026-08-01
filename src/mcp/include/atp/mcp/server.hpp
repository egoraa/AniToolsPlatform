#ifndef ATP_MCP_SERVER_HPP
#define ATP_MCP_SERVER_HPP

#include <exception>
#include <optional>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include <atp/mcp/json_rpc.hpp>
#include <atp/mcp/resource_registry.hpp>
#include <atp/mcp/tool_registry.hpp>

namespace atp::mcp {

/// The single MCP revision this server speaks. Version negotiation is trivial here: the
/// specification requires the server to answer with a version it supports, and it supports one.
inline constexpr const char* protocol_version = "2025-11-25";
inline constexpr const char* server_name = "atp-studio";
inline constexpr const char* server_version = "0.1.0";

/// The MCP layer: it turns decoded JSON-RPC messages into calls on the registries and back. It knows
/// nothing about the studio, which is what makes it testable against stub tools.
class server {
   public:
    /// @param tools registry the tools/* methods are served from; it must outlive the server
    /// @param resources registry the resources/* methods are served from; it must outlive the server
    server(tool_registry& tools, resource_registry& resources) : tools_(&tools), resources_(&resources) {}

    /// Handles one message.
    /// @param message a decoded JSON-RPC message
    /// @return the response, or nullopt for a notification, which must not be answered
    [[nodiscard]] std::optional<nlohmann::json> handle(const nlohmann::json& message) {
        nlohmann::json id;
        try {
            const rpc_request request = parse_request(message);
            id = request.id;
            if (request.notification()) {
                return std::nullopt;
            }
            return make_result(id, dispatch(request));
        } catch (const rpc_error& e) {
            return make_error(id, e.code(), e.what());
        } catch (const std::exception& e) {
            return make_error(id, rpc_internal_error, e.what());
        }
    }

   private:
    [[nodiscard]] nlohmann::json dispatch(const rpc_request& request) {
        if (request.method == "initialize") {
            return initialize();
        }
        if (request.method == "ping") {
            return nlohmann::json::object();
        }
        if (request.method == "tools/list") {
            return nlohmann::json{{"tools", tools_->describe()}};
        }
        if (request.method == "tools/call") {
            return call_tool(request.params);
        }
        if (request.method == "resources/list") {
            return nlohmann::json{{"resources", resources_->describe()}};
        }
        if (request.method == "resources/read") {
            return read_resource(request.params);
        }
        throw rpc_error(rpc_method_not_found, "unknown method '" + request.method + "'");
    }

    [[nodiscard]] static nlohmann::json initialize() {
        return nlohmann::json{
            {"protocolVersion", protocol_version},
            {"capabilities", {{"tools", nlohmann::json::object()}, {"resources", nlohmann::json::object()}}},
            {"serverInfo", {{"name", server_name}, {"version", server_version}}}};
    }

    [[nodiscard]] nlohmann::json call_tool(const nlohmann::json& params) {
        if (!params.contains("name") || !params.at("name").is_string()) {
            throw rpc_error(rpc_invalid_params, "tools/call needs a string \"name\"");
        }
        const auto name = params.at("name").get<std::string>();
        const tool* t = tools_->find(name);
        if (t == nullptr) {
            throw rpc_error(rpc_invalid_params, "unknown tool '" + name + "'");
        }
        const nlohmann::json arguments =
            params.contains("arguments") ? params.at("arguments") : nlohmann::json::object();
        try {
            nlohmann::json payload = t->run(arguments);
            nlohmann::json result{{"content", text_content(payload.dump(2))}, {"isError", false}};
            if (payload.is_object()) {
                result["structuredContent"] = std::move(payload);
            }
            return result;
        } catch (const std::exception& e) {
            return nlohmann::json{{"content", text_content(e.what())}, {"isError", true}};
        }
    }

    [[nodiscard]] nlohmann::json read_resource(const nlohmann::json& params) {
        if (!params.contains("uri") || !params.at("uri").is_string()) {
            throw rpc_error(rpc_invalid_params, "resources/read needs a string \"uri\"");
        }
        const auto uri = params.at("uri").get<std::string>();
        const resource* r = resources_->find(uri);
        if (r == nullptr) {
            throw rpc_error(rpc_invalid_params, "unknown resource '" + uri + "'");
        }
        return nlohmann::json{
            {"contents", nlohmann::json::array({{{"uri", r->uri}, {"mimeType", r->mime_type}, {"text", r->read()}}})}};
    }

    [[nodiscard]] static nlohmann::json text_content(const std::string& text) {
        return nlohmann::json::array({{{"type", "text"}, {"text", text}}});
    }

    tool_registry* tools_;
    resource_registry* resources_;
};

}  // namespace atp::mcp

#endif
