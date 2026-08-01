#include <string>
#include <typeindex>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atp/io/property_codec.hpp>
#include <atp/mcp/arguments.hpp>
#include <atp/mcp/module_json.hpp>
#include <atp/mcp/type_name.hpp>
#include <atp/runtime/config_model.hpp>
#include <atp/studio/module_manager.hpp>

namespace {

TEST(McpArguments, ReadsRequiredAndOptionalFields) {
    const nlohmann::json args = nlohmann::json::parse(R"({"path":"a.json","replay":true,"index":2})");
    EXPECT_EQ(atp::mcp::arg_string(args, "path"), "a.json");
    EXPECT_EQ(atp::mcp::arg_string_or(args, "name", "fallback"), "fallback");
    EXPECT_TRUE(atp::mcp::arg_bool_or(args, "replay", false));
    EXPECT_FALSE(atp::mcp::arg_bool_or(args, "missing", false));
    EXPECT_EQ(atp::mcp::arg_index(args, "index"), 2u);
}

TEST(McpArguments, RefusesMissingAndMistypedFieldsAsConfigErrors) {
    const nlohmann::json args = nlohmann::json::parse(R"({"path":7})");
    EXPECT_THROW((void)atp::mcp::arg_string(args, "path"), atp::runtime::config_error);
    EXPECT_THROW((void)atp::mcp::arg_string(args, "absent"), atp::runtime::config_error);
    EXPECT_THROW((void)atp::mcp::arg_index(args, "absent"), atp::runtime::config_error);
    EXPECT_THROW((void)atp::mcp::arg_index(nlohmann::json{{"index", -1}}, "index"), atp::runtime::config_error);
    EXPECT_THROW((void)atp::mcp::arg_index(nlohmann::json{{"index", 1.5}}, "index"), atp::runtime::config_error);
}

TEST(McpArguments, ReadsAnIndexRegardlessOfHowTheNumberWasStored) {
    EXPECT_EQ(atp::mcp::arg_index(nlohmann::json{{"index", 10}}, "index"), 10u);
    EXPECT_EQ(atp::mcp::arg_index(nlohmann::json::parse(R"({"index":10})"), "index"), 10u);
}

TEST(McpArguments, AcceptsOnlyScalarPropertyValues) {
    EXPECT_EQ(atp::mcp::arg_scalar(nlohmann::json::parse(R"({"value":5})"), "value"), 5);
    EXPECT_EQ(atp::mcp::arg_scalar(nlohmann::json::parse(R"({"value":"5"})"), "value"), "5");
    EXPECT_THROW((void)atp::mcp::arg_scalar(nlohmann::json::parse(R"({"value":{"a":1}})"), "value"),
                 atp::runtime::config_error);
}

TEST(McpArguments, BuildsObjectSchemasWithRequiredNames) {
    const nlohmann::json schema =
        atp::mcp::object_schema({{"group_path", "string", "Group to add to"}, {"name", "string", "Child name", false}});
    EXPECT_EQ(schema.at("type"), "object");
    EXPECT_EQ(schema.at("properties").at("group_path").at("type"), "string");
    EXPECT_EQ(schema.at("properties").at("name").at("description"), "Child name");
    ASSERT_EQ(schema.at("required").size(), 1u);
    EXPECT_EQ(schema.at("required").at(0), "group_path");
    EXPECT_EQ(schema.at("additionalProperties"), false);

    EXPECT_EQ(atp::mcp::no_arguments_schema().at("additionalProperties"), false);
    EXPECT_FALSE(atp::mcp::no_arguments_schema().contains("required"));
}

TEST(McpModuleJson, TurnsPropertyKindsAndOptionsIntoSchema) {
    const atp::studio::property_info number{"limit", atp::io::property_kind::number, "10", {}, true};
    const nlohmann::json number_schema = atp::mcp::property_schema(number);
    EXPECT_EQ(number_schema.at("type"), "number");
    EXPECT_EQ(number_schema.at("default"), "10");
    EXPECT_FALSE(number_schema.contains("enum"));

    const atp::studio::property_info choice{"mode", atp::io::property_kind::text, "fast", {"fast", "slow"}, true};
    const nlohmann::json choice_schema = atp::mcp::property_schema(choice);
    EXPECT_EQ(choice_schema.at("type"), "string");
    ASSERT_EQ(choice_schema.at("enum").size(), 2u);
    EXPECT_EQ(choice_schema.at("enum").at(0), "fast");

    const atp::studio::property_info flag{"on", atp::io::property_kind::boolean, "false", {}, false};
    EXPECT_EQ(atp::mcp::property_schema(flag).at("type"), "boolean");
}

TEST(McpModuleJson, SerialisesAModuleWithItsPortsAndBrokenFlag) {
    atp::studio::module_info info{"demo", atp::version{1, 2}, {}, {}, {}, false, {}};
    info.inputs.push_back({"in", std::type_index(typeid(int))});
    info.outputs.push_back({"out", std::type_index(typeid(double))});
    info.properties.push_back({"limit", atp::io::property_kind::number, "10", {}, true});

    const nlohmann::json json = atp::mcp::to_json(info);
    EXPECT_EQ(json.at("name"), "demo");
    EXPECT_EQ(json.at("version"), "1.2");
    EXPECT_EQ(json.at("broken"), false);
    EXPECT_FALSE(json.contains("error"));
    EXPECT_EQ(json.at("inputs").at(0).at("name"), "in");
    EXPECT_EQ(json.at("inputs").at(0).at("type"), atp::mcp::type_name(std::type_index(typeid(int))));
    EXPECT_EQ(json.at("outputs").at(0).at("name"), "out");
    EXPECT_EQ(json.at("properties").at(0).at("name"), "limit");
    EXPECT_EQ(json.at("properties").at(0).at("persistent"), true);
    EXPECT_EQ(json.at("properties").at(0).at("schema").at("type"), "number");
}

}  // namespace
