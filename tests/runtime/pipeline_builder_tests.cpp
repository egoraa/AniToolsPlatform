// SPDX-License-Identifier: Apache-2.0
#include <array>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <latch>
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <atp/config/node.hpp>
#include <atp/config/read.hpp>
#include <atp/module.hpp>
#include <atp/runtime/config_loader.hpp>
#include <atp/runtime/config_model.hpp>
#include <atp/runtime/config_validator.hpp>
#include <atp/runtime/json_codec.hpp>
#include <atp/runtime/pipeline_builder.hpp>
#include <atp/runtime/utf8_path.hpp>

namespace {

std::filesystem::path unicode_path(std::initializer_list<char32_t> points, std::string_view suffix) {
    std::u32string name(points);
    for (const char c : suffix) {
        name.push_back(static_cast<char32_t>(c));
    }
    return {name};
}

struct feed_outputs : atp::io::outputs {
    atp::io::output<int>& value = make<atp::io::output<int>>("value");
};
struct drain_inputs : atp::io::inputs {
    atp::io::queued_input<int>& value = make<atp::io::queued_input<int>>("value");
};
using feed_ports = atp::ports<atp::io::inputs, feed_outputs>;
using drain_ports = atp::ports<drain_inputs>;

struct sink_props : atp::io::properties {
    atp::io::property<std::string>& tag = make<atp::io::property<std::string>>("tag");
};
using sink_ports = atp::ports<drain_inputs, atp::io::outputs, sink_props>;

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
    atp::io::property<double>& gain = make("gain", 1.0);
};
class limit_sink : public atp::module<atp::ports<atp::io::inputs, atp::io::outputs, limiter_props>, "limiter"> {};

struct channels_config : atp::module_config {
    using module_config::module_config;
    std::int64_t& channels = field("channels", std::int64_t{0});
};

class config_reading_module : public atp::module<atp::ports<>, "config_reader"> {
   public:
    using config_type = channels_config;

    explicit config_reading_module(std::unique_ptr<channels_config> cfg) : channels_(cfg->channels) {}

    [[nodiscard]] std::int64_t channels() const {
        return channels_;
    }

   private:
    std::int64_t channels_;
};

class config_probing_module : public atp::module<atp::ports<>, "config_probe"> {
   public:
    using config_type = channels_config;

    explicit config_probing_module(std::unique_ptr<channels_config> cfg)
        : saw_nothing_(!cfg->find("channels")->is_set()) {}

    [[nodiscard]] bool saw_nothing() const {
        return saw_nothing_;
    }

   private:
    bool saw_nothing_;
};

class config_text_module : public atp::module<atp::ports<>, "config_text"> {
   public:
    using config_type = channels_config;

    explicit config_text_module(std::unique_ptr<channels_config> cfg)
        : text_(cfg->text()), origin_(cfg->origin()), opaque_(cfg->is_opaque()) {}

    [[nodiscard]] const std::string& text() const {
        return text_;
    }

    [[nodiscard]] const std::string& origin() const {
        return origin_;
    }

    [[nodiscard]] bool opaque() const {
        return opaque_;
    }

   private:
    std::string text_;
    std::string origin_;
    bool opaque_;
};

atp::runtime::config make_config(const char* text) {
    const atp::config::node doc = atp::runtime::json_parse(text);
    const auto errors = atp::runtime::validate((doc));
    EXPECT_TRUE(errors.empty());
    return atp::runtime::decode(doc);
}

std::filesystem::path make_temp_dir(const std::string& tag) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("atp_cfg_" + tag);
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

void write_text(const std::filesystem::path& file, std::string_view body) {
    std::ofstream out(file, std::ios::binary);
    out.write(body.data(), static_cast<std::streamsize>(body.size()));
}

void register_config_modules(atp::module_registry& registry) {
    registry.add(std::make_unique<atp::module_factory<config_reading_module>>("config_reader"));
    registry.add(std::make_unique<atp::module_factory<config_text_module>>("config_text"));
}

std::string build_error(const atp::runtime::config& cfg, const std::filesystem::path& base_dir) {
    atp::runtime::pipeline pipe;
    atp::runtime::pipeline_runner runner;
    atp::module_registry registry;
    register_config_modules(registry);
    try {
        atp::runtime::build_pipeline(pipe, runner, cfg, registry, base_dir);
    } catch (const atp::runtime::config_error& e) {
        return e.what();
    }
    return {};
}

TEST(PipelineBuilder, BuildsTreePropertiesAndRuns) {
    const atp::runtime::config cfg = make_config(R"({
        "version": "1.0",
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
        "version": "1.0",
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
        "version": "1.0",
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
        "version": "1.0",
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
        "version": "1.0",
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

TEST(PipelineBuilder, AWholeValuedRealReachesARealProperty) {
    const atp::runtime::config cfg = make_config(R"({
        "version": "1.0",
        "pipeline": {"modules": [{"module": "limiter", "properties": {"gain": 48000.0}}]}
    })");
    atp::runtime::application app;
    app.registry.add<limit_sink>();
    atp::runtime::build(app, cfg, ".");
    auto* m = app.pipe.root().find_module("limiter");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->properties().at("gain").to_string(), "48000");
}

TEST(PipelineBuilder, AWholeValuedRealDoesNotSatisfyAnIntegerProperty) {
    const atp::runtime::config cfg = make_config(R"({
        "version": "1.0",
        "pipeline": {"modules": [{"module": "limiter", "properties": {"limit": 5.0}}]}
    })");
    atp::runtime::application app;
    app.registry.add<limit_sink>();
    try {
        atp::runtime::build(app, cfg, ".");
        FAIL() << "expected config_error";
    } catch (const atp::runtime::config_error& e) {
        EXPECT_NE(std::string(e.what()).find("5.0"), std::string::npos);
    }
}

TEST(PipelineBuilder, ANonScalarPropertyValueNamesThePropertyAndTheForm) {
    const atp::runtime::config cfg = atp::runtime::decode(atp::runtime::json_parse(R"({
        "version": "1.0",
        "pipeline": {"modules": [{"module": "limiter", "properties": {"limit": [1, 2]}}]}
    })"));
    atp::runtime::application app;
    app.registry.add<limit_sink>();
    try {
        atp::runtime::build(app, cfg, ".");
        FAIL() << "expected config_error";
    } catch (const atp::runtime::config_error& e) {
        const std::string text = e.what();
        EXPECT_NE(text.find("limit"), std::string::npos);
        EXPECT_NE(text.find("array"), std::string::npos);
    }
}

TEST(PipelineBuilder, NumericOptionSetIsCheckedToo) {
    atp::runtime::application app;
    app.registry.add<limit_sink>();
    const atp::runtime::config ok = make_config(R"({
        "version": "1.0",
        "pipeline": {"modules": [{"module": "limiter", "properties": {"channels": 6}}]}
    })");
    atp::runtime::build(app, ok, ".");
    EXPECT_EQ(app.pipe.root().find_module("limiter")->properties().at("channels").to_string(), "6");

    atp::runtime::application other;
    other.registry.add<limit_sink>();
    const atp::runtime::config bad = make_config(R"({
        "version": "1.0",
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
        "version": "1.0",
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
        "version": "1.0",
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
        "version": "1.0",
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
        "version": "1.0",
        "pipeline": {"modules": [{"module": "one_shot"}]},
        "threads": [{"name": "t"}]
    })");

    atp::module_registry registry;
    registry.add<one_shot_source>();
    atp::runtime::pipeline pipe;
    atp::runtime::pipeline_runner runner;
    atp::runtime::build_pipeline(pipe, runner, cfg, registry);

    EXPECT_NE(pipe.root().find_module("one_shot"), nullptr);
    runner.start(pipe);
    runner.stop();
}

TEST(PipelineBuilder, InlineConfigReachesTheModule) {
    const atp::runtime::config cfg = make_config(R"({
        "version": "1.0",
        "pipeline": {"modules": [{"module": "config_reader", "config": {"channels": 6}}]}
    })");

    atp::module_registry registry;
    registry.add(std::make_unique<atp::module_factory<config_reading_module>>("config_reader"));
    atp::runtime::pipeline pipe;
    atp::runtime::pipeline_runner runner;
    atp::runtime::build_pipeline(pipe, runner, cfg, registry);

    EXPECT_EQ(dynamic_cast<config_reading_module*>(pipe.root().find_module("config_reader"))->channels(), 6);
}

TEST(PipelineBuilder, ReferencedConfigReachesTheModule) {
    const atp::runtime::config cfg = make_config(R"({
        "version": "1.0",
        "configs": {"rig": {"channels": 6}},
        "pipeline": {"modules": [{"module": "config_reader", "config": "rig"}]}
    })");

    atp::module_registry registry;
    registry.add(std::make_unique<atp::module_factory<config_reading_module>>("config_reader"));
    atp::runtime::pipeline pipe;
    atp::runtime::pipeline_runner runner;
    atp::runtime::build_pipeline(pipe, runner, cfg, registry);

    EXPECT_EQ(dynamic_cast<config_reading_module*>(pipe.root().find_module("config_reader"))->channels(), 6);
}

TEST(PipelineBuilder, AnAbsentConfigLeavesEveryFieldUnwritten) {
    const atp::runtime::config cfg = make_config(R"({
        "version": "1.0",
        "pipeline": {"modules": [{"module": "config_probe"}]}
    })");

    atp::module_registry registry;
    registry.add(std::make_unique<atp::module_factory<config_probing_module>>("config_probe"));
    atp::runtime::pipeline pipe;
    atp::runtime::pipeline_runner runner;
    atp::runtime::build_pipeline(pipe, runner, cfg, registry);

    EXPECT_TRUE(dynamic_cast<config_probing_module*>(pipe.root().find_module("config_probe"))->saw_nothing());
}

TEST(ConfigSource, AJsonFileYieldsATreeAndItsBytes) {
    const std::filesystem::path dir = make_temp_dir("source_json");
    write_text(dir / "rig.json", R"({"gain":3})");

    const atp::runtime::config_source src = atp::runtime::load_config_source("rig.json", dir);

    EXPECT_FALSE(src.opaque);
    EXPECT_EQ(src.text, R"({"gain":3})");
    EXPECT_EQ(src.origin, atp::runtime::path_to_utf8(dir / "rig.json"));
    ASSERT_NE(src.root.find("gain"), nullptr);
    EXPECT_EQ(src.root.find("gain")->as_int(), 3);
}

TEST(ConfigSource, AnUnknownExtensionYieldsBytesAndAnEmptyTree) {
    const std::filesystem::path dir = make_temp_dir("source_opaque");
    write_text(dir / "rig.ini", "rate = 48000\n");

    const atp::runtime::config_source src = atp::runtime::load_config_source("rig.ini", dir);

    EXPECT_TRUE(src.opaque);
    EXPECT_EQ(src.text, "rate = 48000\n");
    EXPECT_TRUE(src.root.is_null());
}

TEST(PipelineBuilderFileConfig, ParsesAJsonFile) {
    const std::filesystem::path dir = make_temp_dir("json");
    write_text(dir / "rig.json", R"({"channels": 6})");
    const atp::runtime::config cfg = make_config(R"({
        "version": "1.0",
        "pipeline": {"modules": [{"module": "config_reader", "config": "file:rig.json"}]}
    })");

    atp::module_registry registry;
    register_config_modules(registry);
    atp::runtime::pipeline pipe;
    atp::runtime::pipeline_runner runner;
    atp::runtime::build_pipeline(pipe, runner, cfg, registry, dir);

    const auto* m = dynamic_cast<config_reading_module*>(pipe.root().find_module("config_reader"));
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->channels(), 6);
}

TEST(PipelineBuilderFileConfig, HandsAnUnknownFormatOverAsText) {
    const std::filesystem::path dir = make_temp_dir("opaque");
    write_text(dir / "rig.ini", "rate = 48000\n");
    const atp::runtime::config cfg = make_config(R"({
        "version": "1.0",
        "pipeline": {"modules": [{"module": "config_text", "config": "file:rig.ini"}]}
    })");

    atp::module_registry registry;
    register_config_modules(registry);
    atp::runtime::pipeline pipe;
    atp::runtime::pipeline_runner runner;
    atp::runtime::build_pipeline(pipe, runner, cfg, registry, dir);

    const auto* m = dynamic_cast<config_text_module*>(pipe.root().find_module("config_text"));
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->text(), "rate = 48000\n");
    EXPECT_TRUE(m->opaque());
    EXPECT_NE(m->origin().find("rig.ini"), std::string::npos);
}

TEST(PipelineBuilderFileConfig, AParsedJsonFileIsNotOpaqueAndStillCarriesItsText) {
    const std::filesystem::path dir = make_temp_dir("both");
    write_text(dir / "rig.json", R"({"channels": 6})");
    const atp::runtime::config cfg = make_config(R"({
        "version": "1.0",
        "pipeline": {"modules": [{"module": "config_text", "config": "file:rig.json"}]}
    })");

    atp::module_registry registry;
    register_config_modules(registry);
    atp::runtime::pipeline pipe;
    atp::runtime::pipeline_runner runner;
    atp::runtime::build_pipeline(pipe, runner, cfg, registry, dir);

    const auto* m = dynamic_cast<config_text_module*>(pipe.root().find_module("config_text"));
    ASSERT_NE(m, nullptr);
    EXPECT_FALSE(m->opaque());
    EXPECT_EQ(m->text(), R"({"channels": 6})");
}

TEST(PipelineBuilderFileConfig, MissingFileIsConfigError) {
    const std::filesystem::path dir = make_temp_dir("missing");
    const atp::runtime::config cfg = make_config(R"({
        "version": "1.0",
        "pipeline": {"modules": [{"module": "config_reader", "config": "file:absent.json"}]}
    })");
    const std::string message = build_error(cfg, dir);
    EXPECT_NE(message.find("cannot open config file"), std::string::npos);
    EXPECT_NE(message.find("absent.json"), std::string::npos);
}

TEST(PipelineBuilderFileConfig, ADirectoryIsConfigError) {
    const std::filesystem::path dir = make_temp_dir("dir");
    std::filesystem::create_directories(dir / "rig.json");
    const atp::runtime::config cfg = make_config(R"({
        "version": "1.0",
        "pipeline": {"modules": [{"module": "config_reader", "config": "file:rig.json"}]}
    })");
    EXPECT_NE(build_error(cfg, dir).find("is a directory"), std::string::npos);
}

TEST(PipelineBuilderFileConfig, BrokenJsonIsConfigErrorNamingThePosition) {
    const std::filesystem::path dir = make_temp_dir("broken");
    write_text(dir / "rig.json", R"({"channels": 6,})");
    const atp::runtime::config cfg = make_config(R"({
        "version": "1.0",
        "pipeline": {"modules": [{"module": "config_reader", "config": "file:rig.json"}]}
    })");
    const std::string message = build_error(cfg, dir);
    EXPECT_NE(message.find("cannot parse config file"), std::string::npos);
    EXPECT_NE(message.find("at line 1"), std::string::npos);
}

TEST(PipelineBuilderFileConfig, AJsonRootThatIsNotAnObjectIsConfigError) {
    const std::filesystem::path dir = make_temp_dir("array");
    write_text(dir / "rig.json", "[1, 2, 3]");
    const atp::runtime::config cfg = make_config(R"({
        "version": "1.0",
        "pipeline": {"modules": [{"module": "config_reader", "config": "file:rig.json"}]}
    })");
    EXPECT_NE(build_error(cfg, dir).find("root of a config must be an object"), std::string::npos);
}

TEST(PipelineBuilderFileConfig, ARelativePathWithoutABaseDirSaysSo) {
    const atp::runtime::config cfg = make_config(R"({
        "version": "1.0",
        "pipeline": {"modules": [{"module": "config_reader", "config": "file:rig.json"}]}
    })");
    EXPECT_NE(build_error(cfg, {}).find("needs the document's directory"), std::string::npos);
}

TEST(PipelineBuilderFileConfig, AnAbsolutePathNeedsNoBaseDir) {
    const std::filesystem::path dir = make_temp_dir("absolute");
    write_text(dir / "rig.json", R"({"channels": 6})");
    atp::config::node doc = atp::runtime::json_parse(R"({
        "version": "1.0",
        "pipeline": {"modules": [{"module": "config_reader", "config": ""}]}
    })");
    doc["pipeline"]["modules"][0]["config"] = "file:" + atp::runtime::path_to_utf8(dir / "rig.json");
    ASSERT_TRUE(atp::runtime::validate((doc)).empty());

    atp::module_registry registry;
    register_config_modules(registry);
    atp::runtime::pipeline pipe;
    atp::runtime::pipeline_runner runner;
    atp::runtime::build_pipeline(pipe, runner, atp::runtime::decode(doc), registry);

    EXPECT_EQ(dynamic_cast<config_reading_module*>(pipe.root().find_module("config_reader"))->channels(), 6);
}

TEST(PipelineBuilderFileConfig, ASharedFileEntryFeedsTwoModules) {
    const std::filesystem::path dir = make_temp_dir("shared");
    write_text(dir / "rig.json", R"({"channels": 6})");
    const atp::runtime::config cfg = make_config(R"({
        "version": "1.0",
        "configs": {"rig": "file:rig.json"},
        "pipeline": {"modules": [{"module": "config_reader", "name": "a", "config": "rig"},
                                 {"module": "config_reader", "name": "b", "config": "rig"}]}
    })");

    atp::module_registry registry;
    register_config_modules(registry);
    atp::runtime::pipeline pipe;
    atp::runtime::pipeline_runner runner;
    atp::runtime::build_pipeline(pipe, runner, cfg, registry, dir);

    EXPECT_EQ(dynamic_cast<config_reading_module*>(pipe.root().find_module("a"))->channels(), 6);
    EXPECT_EQ(dynamic_cast<config_reading_module*>(pipe.root().find_module("b"))->channels(), 6);
}

TEST(PipelineBuilderFileConfig, ACyrillicFileNameIsRead) {
    const std::string utf8_name = "\xd0\xba\xd0\xbe\xd0\xbd\xd1\x84\xd0\xb8\xd0\xb3.json";
    const std::filesystem::path dir = make_temp_dir("cyrillic");
    write_text(dir / atp::runtime::path_from_utf8(utf8_name), R"({"channels": 6})");
    atp::config::node doc = atp::runtime::json_parse(R"({
        "version": "1.0",
        "pipeline": {"modules": [{"module": "config_reader", "config": ""}]}
    })");
    doc["pipeline"]["modules"][0]["config"] = "file:" + utf8_name;
    ASSERT_TRUE(atp::runtime::validate((doc)).empty());

    atp::module_registry registry;
    register_config_modules(registry);
    atp::runtime::pipeline pipe;
    atp::runtime::pipeline_runner runner;
    atp::runtime::build_pipeline(pipe, runner, atp::runtime::decode(doc), registry, dir);

    EXPECT_EQ(dynamic_cast<config_reading_module*>(pipe.root().find_module("config_reader"))->channels(), 6);
}

TEST(PipelineBuilderFileConfig, AFileConfigInsideAnIncludedDocument) {
    const std::filesystem::path dir = make_temp_dir("include");
    write_text(dir / "rig.json", R"({"channels": 6})");
    write_text(dir / "modules.json", R"({"modules": [{"module": "config_reader", "config": "file:rig.json"}]})");
    write_text(dir / "top.json", R"({"version": "1.0", "pipeline": {"$include": "modules.json"}})");

    const atp::config::node doc = atp::runtime::load_config(dir / "top.json");
    ASSERT_TRUE(atp::runtime::validate((doc)).empty());
    EXPECT_EQ(doc.at("pipeline").at("modules")[0].string_at("config"), "file:rig.json");

    atp::module_registry registry;
    register_config_modules(registry);
    atp::runtime::pipeline pipe;
    atp::runtime::pipeline_runner runner;
    atp::runtime::build_pipeline(pipe, runner, atp::runtime::decode(doc), registry, dir);

    EXPECT_EQ(dynamic_cast<config_reading_module*>(pipe.root().find_module("config_reader"))->channels(), 6);
}

TEST(PipelineBuilder, DanglingConfigReferenceIsConfigError) {
    atp::runtime::config cfg = make_config(R"({
        "version": "1.0",
        "configs": {"rig": {"channels": 6}},
        "pipeline": {"modules": [{"module": "config_reader", "config": "rig"}]}
    })");
    cfg.configs.clear();

    atp::module_registry registry;
    registry.add(std::make_unique<atp::module_factory<config_reading_module>>("config_reader"));
    atp::runtime::pipeline pipe;
    atp::runtime::pipeline_runner runner;
    EXPECT_THROW(atp::runtime::build_pipeline(pipe, runner, cfg, registry), atp::runtime::config_error);
}

TEST(PipelineBuilder, ARealPropertyValueKeepsItsPrecisionThroughScalarToString) {
    EXPECT_EQ(atp::runtime::detail::scalar_to_string(atp::config::node(0.1)), "0.1");
    EXPECT_EQ(atp::runtime::detail::scalar_to_string(atp::config::node(48000.0)), "48000.0");
    EXPECT_EQ(atp::runtime::detail::scalar_to_string(atp::config::node(std::int64_t{48000})), "48000");
    EXPECT_EQ(atp::runtime::detail::scalar_to_string(atp::config::node("rig")), "rig");
    EXPECT_EQ(atp::runtime::detail::scalar_to_string(atp::config::node(true)), "true");
}

TEST(PipelineBuilder, PluginDirectoryNamedOutsideAsciiIsResolvedAsUtf8) {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "atp_dir_utf8_root";
    std::filesystem::remove_all(root);
    const std::filesystem::path dir = root / unicode_path({0x043f, 0x043b, 0x0430, 0x0433}, "");
    std::filesystem::create_directories(dir);
    std::filesystem::copy_file(ATP_TEST_PLUGIN, dir / std::filesystem::path(ATP_TEST_PLUGIN).filename());

    atp::runtime::application app;
    const std::string entry = atp::runtime::path_to_utf8(dir.filename());
    ASSERT_NO_THROW(atp::runtime::build(app, config_with_plugins({entry}), root))
        << "a plugins entry is UTF-8 text of the document, and base_dir / p read it as the code page";

    EXPECT_EQ(app.plugins.size(), 1u);
    EXPECT_FALSE(app.registry.versions("plugin_module").empty());
}

}  // namespace
