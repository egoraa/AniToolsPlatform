#include <optional>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atp/mcp/json_rpc.hpp>
#include <atp/mcp/resource_registry.hpp>
#include <atp/mcp/server.hpp>
#include <atp/mcp/tool_registry.hpp>

namespace {

class McpServer : public ::testing::Test {
   protected:
    void SetUp() override {
        tools_.add({"echo", "Echoes its argument", nlohmann::json{{"type", "object"}},
                    [](const nlohmann::json& args) { return nlohmann::json{{"seen", args.at("text")}}; }});
        tools_.add({"boom", "Always fails", nlohmann::json{{"type", "object"}},
                    [](const nlohmann::json&) -> nlohmann::json { throw std::runtime_error("no child 'x'"); }});
        resources_.add({"atp://document", "document", "application/json", [] { return std::string("{\"a\":1}"); }});
    }

    nlohmann::json call(const char* text) {
        const std::optional<nlohmann::json> reply = server_.handle(nlohmann::json::parse(text));
        EXPECT_TRUE(reply.has_value());
        return reply.value_or(nlohmann::json::object());
    }

    atp::mcp::tool_registry tools_;
    atp::mcp::resource_registry resources_;
    atp::mcp::server server_{tools_, resources_};
};

TEST_F(McpServer, AnswersInitializeWithItsOwnProtocolVersion) {
    const nlohmann::json reply = call(R"({"jsonrpc":"2.0","id":1,"method":"initialize",
        "params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"c","version":"1"}}})");
    const nlohmann::json& result = reply.at("result");
    EXPECT_EQ(result.at("protocolVersion"), atp::mcp::protocol_version);
    EXPECT_TRUE(result.at("capabilities").contains("tools"));
    EXPECT_TRUE(result.at("capabilities").contains("resources"));
    EXPECT_EQ(result.at("serverInfo").at("name"), atp::mcp::server_name);
}

TEST_F(McpServer, DoesNotAnswerNotifications) {
    EXPECT_FALSE(server_.handle(nlohmann::json::parse(R"({"jsonrpc":"2.0","method":"notifications/initialized"})")));
}

TEST_F(McpServer, AnswersPingAndListsToolsAndResources) {
    EXPECT_TRUE(call(R"({"jsonrpc":"2.0","id":1,"method":"ping"})").at("result").empty());
    EXPECT_EQ(call(R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})").at("result").at("tools").size(), 2u);
    EXPECT_EQ(call(R"({"jsonrpc":"2.0","id":3,"method":"resources/list"})").at("result").at("resources").size(), 1u);
}

TEST_F(McpServer, CallsAToolAndReturnsTextAndStructuredContent) {
    const nlohmann::json reply =
        call(R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"echo","arguments":{"text":"hi"}}})");
    const nlohmann::json& result = reply.at("result");
    EXPECT_FALSE(result.at("isError").get<bool>());
    EXPECT_EQ(result.at("content").at(0).at("type"), "text");
    EXPECT_NE(result.at("content").at(0).at("text").get<std::string>().find("hi"), std::string::npos);
    EXPECT_EQ(result.at("structuredContent").at("seen"), "hi");
}

TEST_F(McpServer, ReportsAToolFailureAsIsErrorRatherThanAProtocolError) {
    const nlohmann::json reply =
        call(R"({"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"boom","arguments":{}}})");
    EXPECT_FALSE(reply.contains("error"));
    EXPECT_TRUE(reply.at("result").at("isError").get<bool>());
    EXPECT_NE(reply.at("result").at("content").at(0).at("text").get<std::string>().find("no child 'x'"),
              std::string::npos);
}

TEST_F(McpServer, ReportsAnUnknownToolAndAnUnknownMethodAsProtocolErrors) {
    const nlohmann::json unknown_tool =
        call(R"({"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"nope","arguments":{}}})");
    EXPECT_EQ(unknown_tool.at("error").at("code"), atp::mcp::rpc_invalid_params);

    const nlohmann::json unknown_method = call(R"({"jsonrpc":"2.0","id":7,"method":"nope"})");
    EXPECT_EQ(unknown_method.at("error").at("code"), atp::mcp::rpc_method_not_found);
}

TEST_F(McpServer, ReadsAResourceAndRejectsAnUnknownUri) {
    const nlohmann::json reply =
        call(R"({"jsonrpc":"2.0","id":8,"method":"resources/read","params":{"uri":"atp://document"}})");
    const nlohmann::json& contents = reply.at("result").at("contents").at(0);
    EXPECT_EQ(contents.at("uri"), "atp://document");
    EXPECT_EQ(contents.at("mimeType"), "application/json");
    EXPECT_EQ(contents.at("text"), "{\"a\":1}");

    const nlohmann::json missing =
        call(R"({"jsonrpc":"2.0","id":9,"method":"resources/read","params":{"uri":"atp://nope"}})");
    EXPECT_EQ(missing.at("error").at("code"), atp::mcp::rpc_invalid_params);
}

}  // namespace
