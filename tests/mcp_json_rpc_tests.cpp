#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atp/mcp/json_rpc.hpp>

namespace {

TEST(McpJsonRpc, ParsesRequestWithParams) {
    const nlohmann::json message =
        nlohmann::json::parse(R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"x"}})");
    const atp::mcp::rpc_request request = atp::mcp::parse_request(message);
    EXPECT_EQ(request.method, "tools/call");
    EXPECT_EQ(request.params.at("name"), "x");
    EXPECT_EQ(request.id, 1);
    EXPECT_FALSE(request.notification());
}

TEST(McpJsonRpc, TreatsMissingIdAsNotification) {
    const nlohmann::json message = nlohmann::json::parse(R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
    const atp::mcp::rpc_request request = atp::mcp::parse_request(message);
    EXPECT_TRUE(request.notification());
    EXPECT_TRUE(request.params.is_object());  // absent params become an empty object
    EXPECT_TRUE(request.params.empty());
}

TEST(McpJsonRpc, RejectsWrongVersionAndMissingMethod) {
    const nlohmann::json bad_version = nlohmann::json::parse(R"({"jsonrpc":"1.0","id":1,"method":"ping"})");
    EXPECT_THROW((void)atp::mcp::parse_request(bad_version), atp::mcp::rpc_error);
    const nlohmann::json no_method = nlohmann::json::parse(R"({"jsonrpc":"2.0","id":1})");
    EXPECT_THROW((void)atp::mcp::parse_request(no_method), atp::mcp::rpc_error);
}

TEST(McpJsonRpc, CarriesTheCodeOnTheError) {
    try {
        (void)atp::mcp::parse_request(nlohmann::json::parse(R"({"jsonrpc":"2.0","id":1})"));
        FAIL() << "expected rpc_error";
    } catch (const atp::mcp::rpc_error& e) {
        EXPECT_EQ(e.code(), atp::mcp::rpc_invalid_request);
    }
}

TEST(McpJsonRpc, BuildsResultAndErrorEnvelopes) {
    const nlohmann::json result = atp::mcp::make_result(1, nlohmann::json{{"ok", true}});
    EXPECT_EQ(result.at("jsonrpc"), "2.0");
    EXPECT_EQ(result.at("id"), 1);
    EXPECT_EQ(result.at("result").at("ok"), true);
    EXPECT_FALSE(result.contains("error"));

    const nlohmann::json error = atp::mcp::make_error(nullptr, atp::mcp::rpc_parse_error, "broken");
    EXPECT_TRUE(error.at("id").is_null());
    EXPECT_EQ(error.at("error").at("code"), atp::mcp::rpc_parse_error);
    EXPECT_EQ(error.at("error").at("message"), "broken");
    EXPECT_FALSE(error.contains("result"));
}

}  // namespace
