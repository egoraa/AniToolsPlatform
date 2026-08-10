// SPDX-License-Identifier: Apache-2.0
#include <array>
#include <filesystem>
#include <initializer_list>
#include <latch>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atp/module.hpp>
#include <atp/runtime/config_model.hpp>
#include <atp/runtime/config_validator.hpp>
#include <atp/runtime/pipeline_builder.hpp>

namespace {

struct feed_outputs : atp::io::outputs {
    atp::io::output<int>& value = make<atp::io::output<int>>("value");
};
struct drain_inputs : atp::io::inputs {
    atp::io::queued_input<int>& value = make<atp::io::queued_input<int>>("value");
};
using feed_ports = atp::io::ports<atp::io::inputs, feed_outputs>;
using drain_ports = atp::io::ports<drain_inputs>;

struct sink_props : atp::io::properties {
    atp::io::property<std::string>& tag = make<atp::io::property<std::string>>("tag");
};
using sink_ports = atp::io::ports<drain_inputs, atp::io::outputs, sink_props>;

class one_shot_source : public atp::module<feed_ports, "one_shot"> {
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

class recording_sink : public atp::module<sink_ports, "recorder"> {
   public:
    static inline std::latch* delivered = nullptr;

    atp::work_status iterate(std::stop_token) override {
        if (inputs().value.try_pop()) {
            if (delivered) {
                delivered->count_down();
            }
            return atp::work_status::busy;
        }
        return atp::work_status::idle;
    }
};

enum class sink_overflow { drop, block };

}  // namespace

template <>
struct atp::io::enum_names<sink_overflow> {
    static constexpr std::array entries{
        atp::io::enum_entry{sink_overflow::drop, "drop"},
        atp::io::enum_entry{sink_overflow::block, "block"},
    };
};

namespace {

struct limiter_props : atp::io::properties {
    atp::io::property<int>& limit = make<atp::io::property<int>>("limit");
    atp::io::property<sink_overflow>& on_overflow =
        make<atp::io::property<sink_overflow>>("on_overflow", sink_overflow::drop);
    atp::io::property<int>& channels = make<atp::io::property<int>>("channels", 2, atp::io::allowed(1, 2, 6));
};
class limit_sink : public atp::module<atp::io::ports<atp::io::inputs, atp::io::outputs, limiter_props>, "limiter"> {};

class config_reading_module : public atp::module<atp::io::ports<>, "config_reader"> {
   public:
    explicit config_reading_module(const atp::config_value& cfg) : channels_(cfg.int_at("channels", 0)) {}

    [[nodiscard]] std::int64_t channels() const {
        return channels_;
    }

   private:
    std::int64_t channels_;
};

class config_probing_module : public atp::module<atp::io::ports<>, "config_probe"> {
   public:
    explicit config_probing_module(const atp::config_value& cfg) : saw_null_(cfg.is_null()) {}

    [[nodiscard]] bool saw_null() const {
        return saw_null_;
    }

   private:
    bool saw_null_;
};

atp::runtime::config make_config(const char* text) {
    const nlohmann::json doc = nlohmann::json::parse(text);
    const auto errors = atp::runtime::validate(doc);
    EXPECT_TRUE(errors.empty());
    return atp::runtime::decode(doc);
}

TEST(PipelineBuilder, BuildsTreePropertiesAndRuns) {
    const atp::runtime::config cfg = make_config(R"({
        "version": "3.0",
        "pipeline": {
            "modules": [
                {"group": "left", "modules": [{"module": "one_shot", "name": "src"}],
                 "expose": {"outputs": {"out": "src.value"}}},
                {"group": "right", "modules": [{"module": "recorder", "properties": {"tag": "demo"}}],
                 "expose": {"inputs": {"in": "recorder.value"}}}
            ],
            "connections": [{"from": "left.out", "to": "right.in"}]
        },
        "threads": [{"name": "a", "mode": "on_demand"}, {"name": "b", "mode": "on_demand"}],
        "assign": {"left": "a", "right": "b"}
    })");

    atp::runtime::application app;
    app.registry.add<one_shot_source>();
    app.registry.add<recording_sink>();
    atp::runtime::build(app, cfg, ".");

    ASSERT_NE(app.pipe.root().find_group("left"), nullptr);
    ASSERT_NE(app.pipe.root().find_group("right"), nullptr);
    EXPECT_NE(app.pipe.root().find_group("right")->find_module("recorder"), nullptr);

    auto* rec = app.pipe.root().find_group("right")->find_module("recorder");
    ASSERT_NE(rec, nullptr);
    EXPECT_EQ(rec->properties().at("tag").to_string(), "demo");

    std::latch delivered(1);
    recording_sink::delivered = &delivered;
    app.runner.start(app.pipe);
    delivered.wait();
    app.runner.stop();
    recording_sink::delivered = nullptr;
}

TEST(PipelineBuilder, UnknownPropertyIsConfigError) {
    const atp::runtime::config cfg = make_config(R"({
        "version": "3.0",
        "pipeline": {"modules": [{"module": "recorder", "properties": {"ghost": 1}}]}
    })");
    atp::runtime::application app;
    app.registry.add<recording_sink>();
    try {
        atp::runtime::build(app, cfg, ".");
        FAIL() << "expected config_error";
    } catch (const atp::runtime::config_error& e) {
        EXPECT_NE(std::string(e.what()).find("recorder"), std::string::npos);
        EXPECT_NE(std::string(e.what()).find("ghost"), std::string::npos);
    }
}

TEST(PipelineBuilder, UnparsableValueIsConfigError) {
    const atp::runtime::config cfg = make_config(R"({
        "version": "3.0",
        "pipeline": {"modules": [{"module": "limiter", "properties": {"limit": "abc"}}]}
    })");
    atp::runtime::application app;
    app.registry.add<limit_sink>();
    try {
        atp::runtime::build(app, cfg, ".");
        FAIL() << "expected config_error";
    } catch (const atp::runtime::config_error& e) {
        EXPECT_NE(std::string(e.what()).find("limiter"), std::string::npos);
        EXPECT_NE(std::string(e.what()).find("abc"), std::string::npos);
    }
}

TEST(PipelineBuilder, EnumPropertyComesFromConfigAsName) {
    const atp::runtime::config cfg = make_config(R"({
        "version": "3.0",
        "pipeline": {"modules": [{"module": "limiter", "properties": {"on_overflow": "block"}}]}
    })");
    atp::runtime::application app;
    app.registry.add<limit_sink>();
    atp::runtime::build(app, cfg, ".");
    auto* m = app.pipe.root().find_module("limiter");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->properties().at("on_overflow").to_string(), "block");
}

TEST(PipelineBuilder, UnknownEnumNameListsOptions) {
    const atp::runtime::config cfg = make_config(R"({
        "version": "3.0",
        "pipeline": {"modules": [{"module": "limiter", "properties": {"on_overflow": "explode"}}]}
    })");
    atp::runtime::application app;
    app.registry.add<limit_sink>();
    try {
        atp::runtime::build(app, cfg, ".");
        FAIL() << "expected config_error";
    } catch (const atp::runtime::config_error& e) {
        EXPECT_NE(std::string(e.what()).find("explode"), std::string::npos);
        EXPECT_NE(std::string(e.what()).find("drop, block"), std::string::npos);
    }
}

TEST(PipelineBuilder, NumericOptionSetIsCheckedToo) {
    atp::runtime::application app;
    app.registry.add<limit_sink>();
    const atp::runtime::config ok = make_config(R"({
        "version": "3.0",
        "pipeline": {"modules": [{"module": "limiter", "properties": {"channels": 6}}]}
    })");
    atp::runtime::build(app, ok, ".");
    EXPECT_EQ(app.pipe.root().find_module("limiter")->properties().at("channels").to_string(), "6");

    atp::runtime::application other;
    other.registry.add<limit_sink>();
    const atp::runtime::config bad = make_config(R"({
        "version": "3.0",
        "pipeline": {"modules": [{"module": "limiter", "properties": {"channels": 3}}]}
    })");
    try {
        atp::runtime::build(other, bad, ".");
        FAIL() << "expected config_error";
    } catch (const atp::runtime::config_error& e) {
        EXPECT_NE(std::string(e.what()).find("1, 2, 6"), std::string::npos);
    }
}

TEST(PipelineBuilder, WrapsPlatformErrorsWithConfigContext) {
    const atp::runtime::config cfg = make_config(R"({
        "version": "3.0",
        "pipeline": {"modules": [{"module": "no_such_module"}]}
    })");

    atp::runtime::application app;
    try {
        atp::runtime::build(app, cfg, ".");
        FAIL() << "expected config_error";
    } catch (const atp::runtime::config_error& e) {
        EXPECT_NE(std::string(e.what()).find("no_such_module"), std::string::npos);
        EXPECT_NE(std::string(e.what()).find("root"), std::string::npos);
    }
}

TEST(PipelineBuilder, UnknownAssignPathIsConfigError) {
    atp::runtime::config cfg = make_config(R"({
        "version": "3.0",
        "pipeline": {"modules": [{"group": "g", "modules": []}]},
        "threads": [{"name": "t"}]
    })");
    cfg.assignments.emplace_back("ghost", "t");

    atp::runtime::application app;
    EXPECT_THROW(atp::runtime::build(app, cfg, "."), atp::runtime::config_error);
}

std::filesystem::path make_plugin_dir(const std::string& name, std::initializer_list<const char*> files) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    for (const char* file : files) {
        std::filesystem::copy_file(file, dir / std::filesystem::path(file).filename());
    }
    return dir;
}

atp::runtime::config config_with_plugins(std::vector<std::string> plugins) {
    atp::runtime::config cfg = make_config(R"({
        "version": "3.0",
        "pipeline": {"modules": []},
        "threads": [{"name": "t"}]
    })");
    cfg.plugins = std::move(plugins);
    return cfg;
}

TEST(PipelineBuilder, DirectoryLoadsEveryPluginInIt) {
    const std::filesystem::path dir = make_plugin_dir("atp_dir_all", {ATP_TEST_PLUGIN, ATP_TEST_PLUGIN_PORTS});

    atp::runtime::application app;
    atp::runtime::build(app, config_with_plugins({dir.string()}), ".");

    EXPECT_EQ(app.plugins.size(), 2u);
    EXPECT_FALSE(app.registry.versions("plugin_module").empty());
    EXPECT_FALSE(app.registry.versions("plugin_echo").empty());
}

TEST(PipelineBuilder, DirectorySkipsAForeignLibrary) {
    const std::filesystem::path dir = make_plugin_dir("atp_dir_foreign", {ATP_TEST_PLUGIN, ATP_TEST_PLUGIN_EMPTY});

    atp::runtime::application app;
    EXPECT_NO_THROW(atp::runtime::build(app, config_with_plugins({dir.string()}), "."));

    EXPECT_EQ(app.plugins.size(), 1u);
    EXPECT_FALSE(app.registry.versions("plugin_module").empty());
}

TEST(PipelineBuilder, DirectoryStopsOnABrokenPlugin) {
    const std::filesystem::path dir = make_plugin_dir("atp_dir_broken", {ATP_TEST_PLUGIN, ATP_TEST_PLUGIN_BAD_ABI});

    atp::runtime::application app;
    try {
        atp::runtime::build(app, config_with_plugins({dir.string()}), ".");
        FAIL() << "a plugin with a wrong ABI must stop the build";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find("ABI"), std::string::npos);
    }
}

TEST(PipelineBuilder, DirectoryAndAFileInsideItLoadOnce) {
    const std::filesystem::path dir = make_plugin_dir("atp_dir_dedup", {ATP_TEST_PLUGIN});
    const std::filesystem::path inside = dir / std::filesystem::path(ATP_TEST_PLUGIN).filename();

    atp::runtime::application app;
    atp::runtime::build(app, config_with_plugins({dir.string(), inside.string()}), ".");

    EXPECT_EQ(app.plugins.size(), 1u);
    EXPECT_EQ(app.registry.versions("plugin_module").size(), 1u);
}

TEST(PipelineBuilder, DirectoryDoesNotDescendIntoSubdirectories) {
    const std::filesystem::path dir = make_plugin_dir("atp_dir_flat", {ATP_TEST_PLUGIN});
    const std::filesystem::path nested = dir / "nested";
    std::filesystem::create_directories(nested);
    std::filesystem::copy_file(ATP_TEST_PLUGIN_PORTS, nested / std::filesystem::path(ATP_TEST_PLUGIN_PORTS).filename());

    atp::runtime::application app;
    atp::runtime::build(app, config_with_plugins({dir.string()}), ".");

    EXPECT_EQ(app.plugins.size(), 1u);
    EXPECT_TRUE(app.registry.versions("plugin_echo").empty());
}

TEST(PipelineBuilder, BuildsIntoPrefilledRegistry) {
    const atp::runtime::config cfg = make_config(R"({
        "version": "3.0",
        "pipeline": {"modules": [{"module": "one_shot"}]},
        "threads": [{"name": "t"}]
    })");

    atp::module_registry registry;
    registry.add<one_shot_source>();
    atp::pipeline pipe;
    atp::pipeline_runner runner;
    atp::runtime::build_pipeline(pipe, runner, cfg, registry);

    EXPECT_NE(pipe.root().find_module("one_shot"), nullptr);
    runner.start(pipe);
    runner.stop();
}

TEST(PipelineBuilder, InlineConfigReachesTheModule) {
    const atp::runtime::config cfg = make_config(R"({
        "version": "3.2",
        "pipeline": {"modules": [{"module": "config_reader", "config": {"channels": 6}}]}
    })");

    atp::module_registry registry;
    registry.add(std::make_unique<atp::module_factory<config_reading_module>>("config_reader"));
    atp::pipeline pipe;
    atp::pipeline_runner runner;
    atp::runtime::build_pipeline(pipe, runner, cfg, registry);

    EXPECT_EQ(dynamic_cast<config_reading_module*>(pipe.root().find_module("config_reader"))->channels(), 6);
}

TEST(PipelineBuilder, ReferencedConfigReachesTheModule) {
    const atp::runtime::config cfg = make_config(R"({
        "version": "3.2",
        "configs": {"rig": {"channels": 6}},
        "pipeline": {"modules": [{"module": "config_reader", "config": "rig"}]}
    })");

    atp::module_registry registry;
    registry.add(std::make_unique<atp::module_factory<config_reading_module>>("config_reader"));
    atp::pipeline pipe;
    atp::pipeline_runner runner;
    atp::runtime::build_pipeline(pipe, runner, cfg, registry);

    EXPECT_EQ(dynamic_cast<config_reading_module*>(pipe.root().find_module("config_reader"))->channels(), 6);
}

TEST(PipelineBuilder, AbsentConfigIsNullNotAnEmptyObject) {
    const atp::runtime::config cfg = make_config(R"({
        "version": "3.2",
        "pipeline": {"modules": [{"module": "config_probe"}]}
    })");

    atp::module_registry registry;
    registry.add(std::make_unique<atp::module_factory<config_probing_module>>("config_probe"));
    atp::pipeline pipe;
    atp::pipeline_runner runner;
    atp::runtime::build_pipeline(pipe, runner, cfg, registry);

    EXPECT_TRUE(dynamic_cast<config_probing_module*>(pipe.root().find_module("config_probe"))->saw_null());
}

TEST(PipelineBuilder, DanglingConfigReferenceIsConfigError) {
    atp::runtime::config cfg = make_config(R"({
        "version": "3.2",
        "configs": {"rig": {"channels": 6}},
        "pipeline": {"modules": [{"module": "config_reader", "config": "rig"}]}
    })");
    cfg.configs.clear();

    atp::module_registry registry;
    registry.add(std::make_unique<atp::module_factory<config_reading_module>>("config_reader"));
    atp::pipeline pipe;
    atp::pipeline_runner runner;
    EXPECT_THROW(atp::runtime::build_pipeline(pipe, runner, cfg, registry), atp::runtime::config_error);
}

}  // namespace
