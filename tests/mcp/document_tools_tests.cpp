#include <filesystem>
#include <memory>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atp/mcp/document_tools.hpp>
#include <atp/mcp/tool_registry.hpp>
#include <atp/mcp/workspace.hpp>
#include <atp/module.hpp>
#include <atp/runtime/config_model.hpp>

namespace {

struct doc_outputs : atp::io::outputs {
    atp::io::output<int>& value = make<atp::io::output<int>>("value");
};
struct doc_inputs : atp::io::inputs {
    atp::io::queued_input<int>& value = make<atp::io::queued_input<int>>("value");
};
struct wrong_inputs : atp::io::inputs {
    atp::io::queued_input<std::string>& text = make<atp::io::queued_input<std::string>>("text");
};
class doc_source : public atp::module<atp::io::ports<atp::io::inputs, doc_outputs>, "doc_source"> {};
class doc_sink : public atp::module<atp::io::ports<doc_inputs>, "doc_sink"> {};
class doc_wrong : public atp::module<atp::io::ports<wrong_inputs>, "doc_wrong"> {};

class McpDocumentTools : public ::testing::Test {
   protected:
    void SetUp() override {
        root_ = std::filesystem::temp_directory_path() / "atp_mcp_document";
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_);
        ws_ = std::make_unique<atp::mcp::workspace>(root_);
        ws_->modules().registry().add<doc_source>();
        ws_->modules().registry().add<doc_sink>();
        ws_->modules().registry().add<doc_wrong>();
        atp::mcp::register_document_tools(tools_, *ws_);
    }

    void TearDown() override {
        ws_.reset();
        std::filesystem::remove_all(root_);
    }

    nlohmann::json call(const char* name, const nlohmann::json& args = nlohmann::json::object()) {
        const atp::mcp::tool* t = tools_.find(name);
        EXPECT_NE(t, nullptr) << name;
        return t == nullptr ? nlohmann::json::object() : t->run(args);
    }

    std::filesystem::path root_;
    std::unique_ptr<atp::mcp::workspace> ws_;
    atp::mcp::tool_registry tools_;
};

TEST_F(McpDocumentTools, BuildsAGraphAndReadsItBack) {
    call("add_module", {{"group_path", ""}, {"factory", "doc_source"}, {"name", "src"}});
    call("add_module", {{"group_path", ""}, {"factory", "doc_sink"}, {"name", "dst"}});
    call("connect", {{"group_path", ""}, {"from", "src.value"}, {"to", "dst.value"}});

    const nlohmann::json document = call("get_document").at("document");
    const nlohmann::json& modules = document.at("pipeline").at("modules");
    ASSERT_EQ(modules.size(), 2u);
    EXPECT_EQ(modules.at(0).at("module"), "doc_source");
    EXPECT_EQ(modules.at(0).at("name"), "src");
    ASSERT_EQ(document.at("pipeline").at("connections").size(), 1u);
    EXPECT_EQ(document.at("pipeline").at("connections").at(0).at("from"), "src.value");
}

TEST_F(McpDocumentTools, RefusesAConnectionBetweenIncompatiblePorts) {
    call("add_module", {{"group_path", ""}, {"factory", "doc_source"}, {"name", "src"}});
    call("add_module", {{"group_path", ""}, {"factory", "doc_wrong"}, {"name", "bad"}});
    EXPECT_THROW((void)call("connect", {{"group_path", ""}, {"from", "src.value"}, {"to", "bad.text"}}),
                 atp::runtime::config_error);
}

TEST_F(McpDocumentTools, UndoesAndRedoesTheLastEdit) {
    call("add_module", {{"group_path", ""}, {"factory", "doc_source"}, {"name", "src"}});
    EXPECT_EQ(call("undo").at("undone"), true);
    EXPECT_TRUE(call("get_document").at("document").at("pipeline").value("modules", nlohmann::json::array()).empty());
    EXPECT_EQ(call("redo").at("redone"), true);
    EXPECT_EQ(call("get_document").at("document").at("pipeline").at("modules").size(), 1u);
}

TEST_F(McpDocumentTools, ExposesAPortOutOfASubgroupUnderAnAutomaticAlias) {
    call("add_group", {{"group_path", ""}, {"name", "stage"}});
    call("add_module", {{"group_path", "stage"}, {"factory", "doc_source"}, {"name", "src"}});
    const nlohmann::json exposed =
        call("expose_port", {{"group_path", "stage"}, {"port_path", "src.value"}, {"output", true}});
    EXPECT_EQ(exposed.at("alias"), "value");
}

TEST_F(McpDocumentTools, RenamesAnExportedAliasWithoutBreakingTheChainAboveIt) {
    call("add_group", {{"group_path", ""}, {"name", "outer"}});
    call("add_group", {{"group_path", "outer"}, {"name", "inner"}});
    call("add_module", {{"group_path", "outer.inner"}, {"factory", "doc_source"}, {"name", "src"}});
    call("add_module", {{"group_path", ""}, {"factory", "doc_sink"}, {"name", "dst"}});
    call("expose_port", {{"group_path", "outer.inner"}, {"port_path", "src.value"}, {"output", true}});
    call("expose_port", {{"group_path", "outer"}, {"port_path", "inner.value"}, {"output", true}});
    call("connect", {{"group_path", ""}, {"from", "outer.value"}, {"to", "dst.value"}});

    EXPECT_EQ(
        call("rename_expose_output", {{"group_path", "outer.inner"}, {"alias", "value"}, {"new_alias", "numbers"}})
            .at("renamed"),
        true);

    const nlohmann::json document = call("get_document").at("document");
    const nlohmann::json& outer = document.at("pipeline").at("modules").at(0);
    EXPECT_EQ(outer.at("expose").at("outputs").at("value"), "inner.numbers");
    EXPECT_EQ(document.at("pipeline").at("connections").at(0).at("from"), "outer.value");
}

TEST_F(McpDocumentTools, RemovingAnExportedAliasTakesTheChainAboveItWithIt) {
    call("add_group", {{"group_path", ""}, {"name", "outer"}});
    call("add_group", {{"group_path", "outer"}, {"name", "inner"}});
    call("add_module", {{"group_path", "outer.inner"}, {"factory", "doc_source"}, {"name", "src"}});
    call("add_module", {{"group_path", ""}, {"factory", "doc_sink"}, {"name", "dst"}});
    call("expose_port", {{"group_path", "outer.inner"}, {"port_path", "src.value"}, {"output", true}});
    call("expose_port", {{"group_path", "outer"}, {"port_path", "inner.value"}, {"output", true}});
    call("connect", {{"group_path", ""}, {"from", "outer.value"}, {"to", "dst.value"}});

    call("remove_expose_output", {{"group_path", "outer.inner"}, {"alias", "value"}});

    const nlohmann::json document = call("get_document").at("document");
    EXPECT_FALSE(document.at("pipeline").at("modules").at(0).contains("expose"));
    EXPECT_TRUE(document.at("pipeline").value("connections", nlohmann::json::array()).empty());
}

TEST_F(McpDocumentTools, RefusesAPathIntoASubgroupThatExportsNoSuchPort) {
    call("add_group", {{"group_path", ""}, {"name", "outer"}});
    call("add_module", {{"group_path", ""}, {"factory", "doc_sink"}, {"name", "dst"}});

    EXPECT_THROW((void)call("connect", {{"group_path", ""}, {"from", "outer.value"}, {"to", "dst.value"}}),
                 atp::runtime::config_error);
}

TEST_F(McpDocumentTools, SavesAndReopensTheDocumentWithItsLayoutSidecar) {
    call("add_module", {{"group_path", ""}, {"factory", "doc_source"}, {"name", "src"}});
    call("auto_layout", {{"group_path", ""}});
    const nlohmann::json saved = call("save_document", {{"path", "pipeline.json"}});
    EXPECT_TRUE(std::filesystem::exists(saved.at("config").get<std::string>()));
    EXPECT_TRUE(std::filesystem::exists(saved.at("layout").get<std::string>()));

    call("new_document");
    EXPECT_TRUE(call("get_document").at("document").at("pipeline").value("modules", nlohmann::json::array()).empty());
    call("open_document", {{"path", "pipeline.json"}});
    EXPECT_EQ(call("get_document").at("document").at("pipeline").at("modules").size(), 1u);
}

TEST_F(McpDocumentTools, RefusesToSaveOutsideTheRoot) {
    EXPECT_THROW((void)call("save_document", {{"path", "../escape.json"}}), atp::runtime::config_error);
}

TEST_F(McpDocumentTools, MovesAChildIntoAnotherGroupAndDropsItsConnections) {
    call("add_group", {{"group_path", ""}, {"name", "stage"}});
    call("add_module", {{"group_path", ""}, {"factory", "doc_source"}, {"name", "src"}});
    call("add_module", {{"group_path", ""}, {"factory", "doc_sink"}, {"name", "dst"}});
    call("connect", {{"group_path", ""}, {"from", "src.value"}, {"to", "dst.value"}});

    const nlohmann::json moved = call("move_child", {{"from_group", ""}, {"name", "src"}, {"to_group", "stage"}});
    EXPECT_EQ(moved.at("moved"), "src");
    EXPECT_EQ(moved.at("dropped_connections"), 1u);
    EXPECT_EQ(moved.at("dropped_exposes"), 0u);

    const nlohmann::json document = call("get_document").at("document");
    EXPECT_TRUE(document.at("pipeline").value("connections", nlohmann::json::array()).empty());

    EXPECT_THROW((void)call("move_child", {{"from_group", ""}, {"name", "dst"}, {"to_group", "ghost"}}),
                 atp::runtime::config_error);
}

}  // namespace
