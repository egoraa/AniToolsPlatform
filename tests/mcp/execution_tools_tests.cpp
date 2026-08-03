#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atp/mcp/document_tools.hpp>
#include <atp/mcp/execution_tools.hpp>
#include <atp/mcp/resource_registry.hpp>
#include <atp/mcp/resources.hpp>
#include <atp/mcp/tool_registry.hpp>
#include <atp/mcp/workspace.hpp>
#include <atp/module.hpp>
#include <atp/runtime/config_model.hpp>

namespace {

struct run_outputs : atp::io::outputs {
    atp::io::output<int>& value = make<atp::io::output<int>>("value");
};
struct run_inputs : atp::io::inputs {
    atp::io::queued_input<int>& value = make<atp::io::queued_input<int>>("value");
};

class run_source : public atp::module<atp::io::ports<atp::io::inputs, run_outputs>, "run_source"> {
   public:
    atp::work_status iterate(std::stop_token) override {
        if (sent_) {
            return atp::work_status::idle;
        }
        sent_ = true;
        outputs().value(42);
        return atp::work_status::busy;
    }

   private:
    bool sent_ = false;
};

class run_sink : public atp::module<atp::io::ports<run_inputs>, "run_sink"> {
   public:
    atp::work_status iterate(std::stop_token) override {
        return inputs().value.try_pop() ? atp::work_status::busy : atp::work_status::idle;
    }
};

class McpExecutionTools : public ::testing::Test {
   protected:
    void SetUp() override {
        root_ = std::filesystem::temp_directory_path() / "atp_mcp_execution";
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_);
        ws_ = std::make_unique<atp::mcp::workspace>(root_);
        ws_->modules().registry().add<run_source>();
        ws_->modules().registry().add<run_sink>();
        atp::mcp::register_document_tools(tools_, *ws_);
        atp::mcp::register_execution_tools(tools_, *ws_);
        atp::mcp::register_resources(resources_, *ws_);
    }

    void TearDown() override {
        if (ws_) {
            ws_->run_session().stop();
        }
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
    atp::mcp::resource_registry resources_;
};

TEST_F(McpExecutionTools, RunsThePipelineAndShowsTheValueTravellingTheConnection) {
    call("add_module", {{"group_path", ""}, {"factory", "run_source"}, {"name", "src"}});
    call("add_module", {{"group_path", ""}, {"factory", "run_sink"}, {"name", "dst"}});
    call("connect", {{"group_path", ""}, {"from", "src.value"}, {"to", "dst.value"}});

    EXPECT_EQ(call("run").at("running"), true);
    EXPECT_EQ(call("get_status").at("running"), true);

    nlohmann::json seen;
    for (int i = 0; i < 500 && seen.is_null(); ++i) {
        const nlohmann::json snapshot = call("read_connections");
        for (const nlohmann::json& c : snapshot.at("connections")) {
            if (c.at("writes").get<std::uint64_t>() > 0) {
                seen = c.at("value");
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    ASSERT_FALSE(seen.is_null());
    EXPECT_EQ(seen.at("value"), 42);

    call("stop");
    EXPECT_EQ(call("get_status").at("running"), false);
}

TEST_F(McpExecutionTools, MeasuresModulesOnlyOnceMetricsAreEnabled) {
    call("add_module", {{"group_path", ""}, {"factory", "run_source"}, {"name", "src"}});
    call("add_module", {{"group_path", ""}, {"factory", "run_sink"}, {"name", "dst"}});
    call("connect", {{"group_path", ""}, {"from", "src.value"}, {"to", "dst.value"}});
    call("run");

    const nlohmann::json before = call("read_module_metrics");
    EXPECT_EQ(before.at("enabled"), false);
    for (const nlohmann::json& m : before.at("modules")) {
        EXPECT_EQ(m.at("calls").get<std::uint64_t>(), 0U);
    }

    EXPECT_EQ(call("set_module_metrics", {{"enabled", true}}).at("enabled"), true);

    nlohmann::json measured;
    for (int i = 0; i < 500 && measured.is_null(); ++i) {
        const nlohmann::json snapshot = call("read_module_metrics");
        for (const nlohmann::json& m : snapshot.at("modules")) {
            if (m.at("calls").get<std::uint64_t>() > 0) {
                measured = m;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    ASSERT_FALSE(measured.is_null());
    EXPECT_TRUE(measured.at("path").get<std::string>() == "src" || measured.at("path").get<std::string>() == "dst");
    EXPECT_GE(measured.at("total_ns").get<std::int64_t>(), measured.at("max_ns").get<std::int64_t>());

    EXPECT_EQ(call("set_module_metrics", {{"enabled", false}}).at("enabled"), false);
    EXPECT_EQ(call("read_module_metrics").at("enabled"), false);
    call("stop");
}

TEST_F(McpExecutionTools, RefusesToMeasureWhenNothingRuns) {
    EXPECT_THROW((void)call("set_module_metrics", {{"enabled", true}}), atp::runtime::config_error);
    EXPECT_TRUE(call("read_module_metrics").at("modules").empty());
}

TEST_F(McpExecutionTools, ReportsABuildFailureAndStaysUsable) {
    call("add_module", {{"group_path", ""}, {"factory", "run_source"}, {"name", "src"}});
    call("add_module", {{"group_path", ""}, {"factory", "run_sink"}, {"name", "dst"}});
    call("connect", {{"group_path", ""}, {"from", "src.value"}, {"to", "dst.value"}});
    call("disconnect", {{"group_path", ""}, {"index", 0}});
    call("remove_child", {{"group_path", ""}, {"name", "dst"}});
    call("connect", {{"group_path", ""}, {"from", "src.value"}, {"to", "src.value"}});

    EXPECT_THROW((void)call("run"), atp::runtime::config_error);
    EXPECT_EQ(call("get_status").at("running"), false);
}

TEST_F(McpExecutionTools, RefusesLiveOperationsWhileStopped) {
    EXPECT_THROW((void)call("set_live_property", {{"path", "src.limit"}, {"value", 1}}), std::logic_error);
    EXPECT_THROW((void)call("sync_persistent_properties"), atp::runtime::config_error);
}

TEST_F(McpExecutionTools, ExposesTheDocumentAndCatalogAsResources) {
    call("add_module", {{"group_path", ""}, {"factory", "run_source"}, {"name", "src"}});

    const atp::mcp::resource* document = resources_.find("atp://document");
    ASSERT_NE(document, nullptr);
    EXPECT_EQ(nlohmann::json::parse(document->read()).at("pipeline").at("modules").at(0).at("module"), "run_source");

    const atp::mcp::resource* modules = resources_.find("atp://modules");
    ASSERT_NE(modules, nullptr);
    EXPECT_EQ(nlohmann::json::parse(modules->read()).size(), 2u);

    const atp::mcp::resource* docs = resources_.find("atp://docs/architecture");
    ASSERT_NE(docs, nullptr);
    EXPECT_FALSE(docs->read().empty());
}

}  // namespace
