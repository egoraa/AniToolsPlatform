// SPDX-License-Identifier: Apache-2.0
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atp/mcp/resource_registry.hpp>
#include <atp/mcp/tool_registry.hpp>

namespace {

TEST(McpToolRegistry, DescribesToolsInRegistrationOrderAndFindsThemByName) {
    atp::mcp::tool_registry tools;
    tools.add({"beta", "second", nlohmann::json{{"type", "object"}},
               [](const nlohmann::json&) { return nlohmann::json{{"n", 2}}; }});
    tools.add({"alpha", "first", nlohmann::json{{"type", "object"}},
               [](const nlohmann::json&) { return nlohmann::json{{"n", 1}}; }});

    const nlohmann::json described = tools.describe();
    ASSERT_EQ(described.size(), 2u);
    EXPECT_EQ(described.at(0).at("name"), "beta");
    EXPECT_EQ(described.at(0).at("description"), "second");
    EXPECT_EQ(described.at(0).at("inputSchema").at("type"), "object");
    EXPECT_EQ(described.at(1).at("name"), "alpha");

    const atp::mcp::tool* found = tools.find("alpha");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->run(nlohmann::json::object()).at("n"), 1);
    EXPECT_EQ(tools.find("missing"), nullptr);
}

TEST(McpResourceRegistry, DescribesResourcesAndReadsByUri) {
    atp::mcp::resource_registry resources;
    resources.add({"atp://document", "document", "application/json", [] { return std::string("{}"); }});

    const nlohmann::json described = resources.describe();
    ASSERT_EQ(described.size(), 1u);
    EXPECT_EQ(described.at(0).at("uri"), "atp://document");
    EXPECT_EQ(described.at(0).at("name"), "document");
    EXPECT_EQ(described.at(0).at("mimeType"), "application/json");

    const atp::mcp::resource* found = resources.find("atp://document");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->read(), "{}");
    EXPECT_EQ(resources.find("atp://nope"), nullptr);
}

}  // namespace
