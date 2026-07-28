#include <any>
#include <chrono>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atp/module.hpp>
#include <atp/runtime/config_model.hpp>
#include <atp/runtime/config_validator.hpp>
#include <atp/studio/module_manager.hpp>
#include <atp/studio/session.hpp>
#include <atp/studio/value_format.hpp>

namespace {

TEST(ValueFormat, FormatsCommonTypesAndRefusesUnknown) {
    EXPECT_EQ(atp::studio::format_value(std::any(42)), "42");
    EXPECT_EQ(atp::studio::format_value(std::any(2.5)), "2.5");
    EXPECT_EQ(atp::studio::format_value(std::any(true)), "true");
    EXPECT_EQ(atp::studio::format_value(std::any(std::string("hi"))), "hi");
    struct opaque {};
    EXPECT_EQ(atp::studio::format_value(std::any(opaque{})), std::nullopt);
}

struct feed_outputs : atp::io::outputs {
    atp::io::output<int>& value = make<atp::io::output<int>>("value");
};
struct drain_inputs : atp::io::inputs {
    atp::io::queued_input<int>& value = make<atp::io::queued_input<int>>("value");
};
using feed_ports = atp::io::ports<atp::io::inputs, feed_outputs>;
using drain_ports = atp::io::ports<drain_inputs>;

class studio_source : public atp::module<feed_ports, "studio_source"> {
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

class studio_sink : public atp::module<drain_ports, "studio_sink"> {
   public:
    atp::work_status iterate(std::stop_token) override {
        return inputs().value.try_pop() ? atp::work_status::busy : atp::work_status::idle;
    }
};

// A module carrying a property: on-the-fly edits are checked against the live tree.
struct target_props : atp::io::properties {
    atp::io::property<int>& limit = make<atp::io::property<int>>("limit", 10);
};
class target_module : public atp::module<atp::io::ports<atp::io::inputs, atp::io::outputs, target_props>, "target"> {};

atp::runtime::config make_config(const char* text) {
    const nlohmann::json doc = nlohmann::json::parse(text);
    EXPECT_TRUE(atp::runtime::validate(doc).empty());
    return atp::runtime::decode(doc);
}

atp::runtime::config session_config() {
    const nlohmann::json doc = nlohmann::json::parse(R"({
        "version": "2.0",
        "pipeline": {
            "modules": [
                {"group": "left", "modules": [{"module": "studio_source", "name": "src"}],
                 "expose": {"outputs": {"out": "src.value"}}},
                {"group": "right", "modules": [{"module": "studio_sink", "name": "sink"}],
                 "expose": {"inputs": {"in": "sink.value"}}}
            ],
            "connections": [{"from": "left.out", "to": "right.in"}]
        }
    })");
    EXPECT_TRUE(atp::runtime::validate(doc).empty());
    return atp::runtime::decode(doc);
}

TEST(StudioSession, RunsConfigSamplesConnectionsAndRestarts) {
    atp::studio::module_manager manager;
    manager.registry().add<studio_source>();
    manager.registry().add<studio_sink>();

    atp::studio::session s(manager.registry());
    EXPECT_FALSE(s.running());
    EXPECT_TRUE(s.stats().empty());

    const atp::runtime::config cfg = session_config();
    s.start(cfg);
    EXPECT_TRUE(s.running());

    // wait for a value to travel along the root's only connection; the timeout is generous
    std::optional<std::string> seen;
    for (int i = 0; i < 500 && !seen; ++i) {
        for (const auto& sample : s.sample_connections()) {
            if (sample.writes > 0 && sample.value) {
                EXPECT_EQ(sample.group_path, "");  // the connection is declared in the root
                EXPECT_EQ(sample.index, 0u);
                seen = atp::studio::format_value(*sample.value);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    EXPECT_EQ(seen, "42");
    EXPECT_FALSE(s.stats().empty());

    s.stop();
    EXPECT_FALSE(s.running());
    s.start(cfg);  // a second run gets a fresh pipeline
    s.stop();
}

TEST(StudioSession, BuildFailureLeavesSessionClean) {
    atp::studio::module_manager manager;
    atp::studio::session s(manager.registry());

    const atp::runtime::config cfg = session_config();  // the modules are not registered
    EXPECT_THROW(s.start(cfg), atp::runtime::config_error);
    EXPECT_FALSE(s.running());
    EXPECT_TRUE(s.sample_connections().empty());

    manager.registry().add<studio_source>();
    manager.registry().add<studio_sink>();
    s.start(cfg);  // the session is usable after a failure
    s.stop();
}

TEST(StudioSession, SetPropertyReachesLiveModule) {
    atp::module_registry registry;
    registry.add<target_module>();
    atp::studio::session s(registry);
    EXPECT_THROW(s.set_property({"target", "limit", "5"}), std::logic_error);  // nothing is running
    s.start(make_config(R"({"version": "2.0", "pipeline": {"modules": [{"module": "target"}]}})"));
    s.set_property({"target", "limit", "42"});
    auto* m = s.live_root()->find_module("target");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->properties().at("limit").to_string(), "42");
    s.stop();
    EXPECT_EQ(s.live_root(), nullptr) << "there is no live tree after stop";
}

}  // namespace
