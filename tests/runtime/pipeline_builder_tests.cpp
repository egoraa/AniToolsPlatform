#include <array>
#include <latch>
#include <stop_token>
#include <string>
#include <utility>

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

enum class overflow_policy { drop, block };

}  // namespace

template <>
struct atp::io::enum_names<overflow_policy> {
    static constexpr std::array entries{
        atp::io::enum_entry{overflow_policy::drop, "drop"},
        atp::io::enum_entry{overflow_policy::block, "block"},
    };
};

namespace {

struct limiter_props : atp::io::properties {
    atp::io::property<int>& limit = make<atp::io::property<int>>("limit");
    atp::io::property<overflow_policy>& on_overflow =
        make<atp::io::property<overflow_policy>>("on_overflow", overflow_policy::drop);
    atp::io::property<int>& channels = make<atp::io::property<int>>("channels", 2, atp::io::allowed(1, 2, 6));
};
class limit_sink : public atp::module<atp::io::ports<atp::io::inputs, atp::io::outputs, limiter_props>, "limiter"> {};

atp::runtime::config make_config(const char* text) {
    const nlohmann::json doc = nlohmann::json::parse(text);
    const auto errors = atp::runtime::validate(doc);
    EXPECT_TRUE(errors.empty());
    return atp::runtime::decode(doc);
}

TEST(PipelineBuilder, BuildsTreePropertiesAndRuns) {
    const atp::runtime::config cfg = make_config(R"({
        "version": "2.0",
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
        "version": "2.0",
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
        "version": "2.0",
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
        "version": "2.0",
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
        "version": "2.0",
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
        "version": "2.0",
        "pipeline": {"modules": [{"module": "limiter", "properties": {"channels": 6}}]}
    })");
    atp::runtime::build(app, ok, ".");
    EXPECT_EQ(app.pipe.root().find_module("limiter")->properties().at("channels").to_string(), "6");

    atp::runtime::application other;
    other.registry.add<limit_sink>();
    const atp::runtime::config bad = make_config(R"({
        "version": "2.0",
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
        "version": "2.0",
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
        "version": "2.0",
        "pipeline": {"modules": [{"group": "g", "modules": []}]},
        "threads": [{"name": "t"}]
    })");
    cfg.assignments.emplace_back("ghost", "t");

    atp::runtime::application app;
    EXPECT_THROW(atp::runtime::build(app, cfg, "."), atp::runtime::config_error);
}

TEST(PipelineBuilder, BuildsIntoPrefilledRegistry) {
    const atp::runtime::config cfg = make_config(R"({
        "version": "2.0",
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

}  // namespace
