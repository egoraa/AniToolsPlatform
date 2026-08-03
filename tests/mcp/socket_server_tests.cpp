#include <chrono>
#include <optional>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atp/mcp/socket_server.hpp>

#include "support/loopback_client.hpp"

namespace {

TEST(McpSocketServer, AnswersOnThePortItPickedItself) {
    atp::mcp::socket_server server(0, [](const nlohmann::json& message) -> std::optional<nlohmann::json> {
        return nlohmann::json{{"echo", message.at("hello")}};
    });
    ASSERT_NE(server.port(), 0);

    atp_tests::loopback_client client(server.port());
    const std::string reply = client.exchange(R"({"hello": "world"})");
    EXPECT_EQ(nlohmann::json::parse(reply).at("echo"), "world");
}

TEST(McpSocketServer, KeepsTheConnectionOpenForMoreThanOneLine) {
    atp::mcp::socket_server server(0, [](const nlohmann::json& message) -> std::optional<nlohmann::json> {
        return nlohmann::json{{"n", message.at("n").get<int>() + 1}};
    });

    atp_tests::loopback_client client(server.port());
    EXPECT_EQ(nlohmann::json::parse(client.exchange(R"({"n": 1})")).at("n"), 2);
    EXPECT_EQ(nlohmann::json::parse(client.exchange(R"({"n": 41})")).at("n"), 42);
}

TEST(McpSocketServer, AnswersUnparsableInputWithAnErrorInsteadOfDying) {
    atp::mcp::socket_server server(
        0, [](const nlohmann::json&) -> std::optional<nlohmann::json> { return nlohmann::json{{"ok", true}}; });

    atp_tests::loopback_client client(server.port());
    EXPECT_TRUE(nlohmann::json::parse(client.exchange("{not json")).contains("error"));
    EXPECT_EQ(nlohmann::json::parse(client.exchange(R"({"n": 1})")).at("ok"), true);
}

TEST(McpSocketServer, ServesASecondClientOnceTheFirstIsGone) {
    atp::mcp::socket_server server(
        0, [](const nlohmann::json&) -> std::optional<nlohmann::json> { return nlohmann::json{{"ok", true}}; });

    {
        atp_tests::loopback_client first(server.port());
        EXPECT_EQ(nlohmann::json::parse(first.exchange(R"({"n": 1})")).at("ok"), true);
    }
    atp_tests::loopback_client second(server.port());
    EXPECT_EQ(nlohmann::json::parse(second.exchange(R"({"n": 1})")).at("ok"), true);
}

TEST(McpSocketServer, WritesNothingBackForANotification) {
    atp::mcp::socket_server server(0, [](const nlohmann::json& message) -> std::optional<nlohmann::json> {
        if (message.contains("silent")) {
            return std::nullopt;
        }
        return nlohmann::json{{"ok", true}};
    });

    atp_tests::loopback_client client(server.port());
    const std::string reply = client.exchange("{\"silent\": 1}\n{\"n\": 1}");
    EXPECT_EQ(nlohmann::json::parse(reply).at("ok"), true);
}

TEST(McpSocketServer, StopsPromptlyEvenWithNoClient) {
    atp::mcp::socket_server server(
        0, [](const nlohmann::json&) -> std::optional<nlohmann::json> { return nlohmann::json::object(); });

    const auto started = std::chrono::steady_clock::now();
    server.stop();
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(2));
    server.stop();
}

}  // namespace
