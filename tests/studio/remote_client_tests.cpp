#include <chrono>
#include <optional>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atp/mcp/json_rpc.hpp>
#include <atp/mcp/socket_server.hpp>
#include <atp/studio/remote_client.hpp>

namespace {

using namespace std::chrono_literals;

nlohmann::json result_for(const nlohmann::json& message, nlohmann::json payload) {
    return atp::mcp::make_result(
        message.at("id"),
        {{"content", nlohmann::json::array()}, {"isError", false}, {"structuredContent", std::move(payload)}});
}

TEST(StudioRemoteClient, ReturnsTheStructuredContentOfATool) {
    atp::mcp::socket_server server(0, [](const nlohmann::json& message) -> std::optional<nlohmann::json> {
        return result_for(message, {{"running", true}, {"tool", message.at("params").at("name")}});
    });

    atp::studio::remote_client client("127.0.0.1", server.port(), 2000ms);
    const nlohmann::json answer = client.call("get_status");
    EXPECT_EQ(answer.at("running"), true);
    EXPECT_EQ(answer.at("tool"), "get_status");
    EXPECT_TRUE(client.connected());
}

TEST(StudioRemoteClient, PassesTheArgumentsThrough) {
    atp::mcp::socket_server server(0, [](const nlohmann::json& message) -> std::optional<nlohmann::json> {
        return result_for(message, {{"echo", message.at("params").at("arguments")}});
    });

    atp::studio::remote_client client("127.0.0.1", server.port(), 2000ms);
    const nlohmann::json answer = client.call("set_live_property", {{"path", "a.b"}, {"value", 3}});
    EXPECT_EQ(answer.at("echo").at("path"), "a.b");
    EXPECT_EQ(answer.at("echo").at("value"), 3);
}

TEST(StudioRemoteClient, TurnsAToolErrorIntoAnException) {
    atp::mcp::socket_server server(0, [](const nlohmann::json& message) -> std::optional<nlohmann::json> {
        return atp::mcp::make_result(
            message.at("id"), {{"content", nlohmann::json::array({{{"type", "text"}, {"text", "no such property"}}})},
                               {"isError", true}});
    });

    atp::studio::remote_client client("127.0.0.1", server.port(), 2000ms);
    try {
        (void)client.call("set_live_property");
        FAIL() << "expected remote_error";
    } catch (const atp::studio::remote_error& e) {
        EXPECT_NE(std::string(e.what()).find("no such property"), std::string::npos);
    }
}

TEST(StudioRemoteClient, StaysUsableAfterAToolRefusedSomething) {
    atp::mcp::socket_server server(0, [](const nlohmann::json& message) -> std::optional<nlohmann::json> {
        if (message.at("params").at("name") == "bad") {
            return atp::mcp::make_result(message.at("id"), {{"content", nlohmann::json::array()}, {"isError", true}});
        }
        return result_for(message, {{"ok", true}});
    });

    atp::studio::remote_client client("127.0.0.1", server.port(), 2000ms);
    EXPECT_THROW((void)client.call("bad"), atp::studio::remote_error);
    EXPECT_TRUE(client.connected());
    EXPECT_EQ(client.call("good").at("ok"), true);
}

TEST(StudioRemoteClient, GivesUpAfterTheTimeoutInsteadOfBlocking) {
    atp::mcp::socket_server server(0,
                                   [](const nlohmann::json&) -> std::optional<nlohmann::json> { return std::nullopt; });

    atp::studio::remote_client client("127.0.0.1", server.port(), 300ms);
    const auto started = std::chrono::steady_clock::now();
    EXPECT_THROW((void)client.call("get_status"), atp::studio::remote_error);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    EXPECT_GE(elapsed, 250ms);
    EXPECT_LT(elapsed, 5s);
    EXPECT_FALSE(client.connected());
}

TEST(StudioRemoteClient, RefusesAnEndpointThatIsNotListening) {
    EXPECT_THROW(atp::studio::remote_client("127.0.0.1", 1, 300ms), atp::studio::remote_error);
}

}  // namespace
