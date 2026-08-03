#ifndef ATP_MCP_JSON_RPC_HPP
#define ATP_MCP_JSON_RPC_HPP

#include <stdexcept>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

/// Transport and protocol layer of the studio's MCP server.
namespace atp::mcp {

/// Standard JSON-RPC 2.0 error codes.
inline constexpr int rpc_parse_error = -32700;
inline constexpr int rpc_invalid_request = -32600;
inline constexpr int rpc_method_not_found = -32601;
inline constexpr int rpc_invalid_params = -32602;
inline constexpr int rpc_internal_error = -32603;

/// A protocol-level failure: the message itself is unusable. It lives next to the module it belongs
/// to, the way runtime::config_error lives in config_model.hpp. A failing tool is a different
/// channel entirely — that travels inside a successful result with isError set.
class rpc_error : public std::runtime_error {
   public:
    /// @param code one of the rpc_* constants
    /// @param message text handed to the client as error.message
    rpc_error(int code, const std::string& message) : std::runtime_error(message), code_(code) {}

    /// JSON-RPC error code to report.
    [[nodiscard]] int code() const noexcept {
        return code_;
    }

   private:
    int code_;
};

/// A parsed JSON-RPC 2.0 request.
struct rpc_request {
    std::string method;
    nlohmann::json params = nlohmann::json::object();
    nlohmann::json id;

    /// Whether the message is a notification, which must not be answered.
    [[nodiscard]] bool notification() const {
        return id.is_null();
    }
};

/// Validates the envelope and pulls the fields out of it.
/// @param message one decoded JSON-RPC message
/// @return the parsed request
/// @throws rpc_error if the version is wrong, the method is missing or params is not an object
[[nodiscard]] inline rpc_request parse_request(const nlohmann::json& message) {
    if (!message.is_object()) {
        throw rpc_error(rpc_invalid_request, "a JSON-RPC message must be an object");
    }
    if (message.value("jsonrpc", std::string{}) != "2.0") {
        throw rpc_error(rpc_invalid_request, R"(expected "jsonrpc": "2.0")");
    }
    if (!message.contains("method") || !message.at("method").is_string()) {
        throw rpc_error(rpc_invalid_request, "missing string field \"method\"");
    }
    rpc_request request;
    request.method = message.at("method").get<std::string>();
    if (message.contains("params")) {
        if (!message.at("params").is_object()) {
            throw rpc_error(rpc_invalid_params, "\"params\" must be an object");
        }
        request.params = message.at("params");
    }
    if (message.contains("id")) {
        request.id = message.at("id");
    }
    return request;
}

/// Wraps a payload into a successful response envelope.
/// @param id the id of the request being answered
/// @param result the payload
[[nodiscard]] inline nlohmann::json make_result(const nlohmann::json& id, nlohmann::json result) {
    return nlohmann::json{{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
}

/// Wraps a protocol failure into an error response envelope. The id is echoed even when it is null,
/// which is what a message that failed to parse leaves us with.
/// @param id the id of the request being answered, or null
/// @param code one of the rpc_* constants
/// @param message text for the client
[[nodiscard]] inline nlohmann::json make_error(const nlohmann::json& id, int code, const std::string& message) {
    return nlohmann::json{{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
}

}  // namespace atp::mcp

#endif
