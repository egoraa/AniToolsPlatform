// SPDX-License-Identifier: Apache-2.0
#include <any>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atp/config/node.hpp>
#include <atp/config/read.hpp>
#include <atp/module.hpp>
#include <atp/runtime/config_model.hpp>
#include <atp/runtime/config_validator.hpp>
#include <atp/runtime/json_codec.hpp>
#include <atp/studio/module_manager.hpp>
#include <atp/studio/session.hpp>

namespace {

struct feed_outputs : atp::io::outputs {
    atp::io::output<int>& value = make<atp::io::output<int>>("value");
};
struct drain_inputs : atp::io::inputs {
    atp::io::queued_input<int>& value = make<atp::io::queued_input<int>>("value");
};
using feed_ports = atp::ports<atp::io::inputs, feed_outputs>;
using drain_ports = atp::ports<drain_inputs>;

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

struct target_props : atp::io::properties {
    atp::io::property<int>& limit = make<atp::io::property<int>>("limit", 10);
};
class target_module : public atp::module<atp::ports<atp::io::inputs, atp::io::outputs, target_props>, "target"> {};

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

atp::runtime::config make_config(const char* text) {
    const atp::config::node proj = atp::runtime::json_parse(text);
    EXPECT_TRUE(atp::runtime::validate((proj)).empty());
    return atp::runtime::decode(proj);
}

atp::runtime::config session_config() {
    const atp::config::node proj = atp::runtime::json_parse(R"({
        "version": "1.0",
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
    EXPECT_TRUE(atp::runtime::validate((proj)).empty());
    return atp::runtime::decode(proj);
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

    bool seen = false;
    for (int i = 0; i < 500 && !seen; ++i) {
        for (const auto& sample : s.sample_connections()) {
            if (sample.writes > 0) {
                EXPECT_EQ(sample.group_path, "");
                EXPECT_EQ(sample.index, 0u);
                seen = true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    EXPECT_TRUE(seen);
    EXPECT_FALSE(s.stats().empty());

    s.stop();
    EXPECT_FALSE(s.running());
    s.start(cfg);
    s.stop();
}

TEST(StudioSession, BuildFailureLeavesSessionClean) {
    atp::studio::module_manager manager;
    atp::studio::session s(manager.registry());

    const atp::runtime::config cfg = session_config();
    EXPECT_THROW(s.start(cfg), atp::runtime::config_error);
    EXPECT_FALSE(s.running());
    EXPECT_TRUE(s.sample_connections().empty());

    manager.registry().add<studio_source>();
    manager.registry().add<studio_sink>();
    s.start(cfg);
    s.stop();
}

TEST(StudioSession, AFileConfigResolvesAgainstTheProjectDirectory) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "atp_session_file_config";
    std::error_code ignored;
    std::filesystem::remove_all(dir, ignored);
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "rig.json", std::ios::binary) << R"({"channels": 6})";

    atp::studio::module_manager manager;
    manager.registry().add(std::make_unique<atp::module_factory<config_reading_module>>("config_reader"));

    atp::studio::session s(manager.registry());
    const atp::runtime::config cfg = make_config(R"({
        "version": "1.0",
        "pipeline": {"modules": [{"module": "config_reader", "config": "file:rig.json"}]}
    })");

    s.start(cfg, dir);
    ASSERT_NE(s.live_root(), nullptr);
    EXPECT_EQ(dynamic_cast<config_reading_module*>(s.live_root()->find_module("config_reader"))->channels(), 6);
    s.stop();

    std::filesystem::remove_all(dir, ignored);
}

TEST(StudioSession, AnUnsavedProjectSaysWhyARelativeFileConfigCannotResolve) {
    atp::studio::module_manager manager;
    manager.registry().add(std::make_unique<atp::module_factory<config_reading_module>>("config_reader"));

    atp::studio::session s(manager.registry());
    const atp::runtime::config cfg = make_config(R"({
        "version": "1.0",
        "pipeline": {"modules": [{"module": "config_reader", "config": "file:rig.json"}]}
    })");

    try {
        s.start(cfg);
        FAIL() << "a relative file config without a project directory has to be refused";
    } catch (const atp::runtime::config_error& e) {
        EXPECT_NE(std::string(e.what()).find("needs the document's directory"), std::string::npos) << e.what();
    }
    EXPECT_FALSE(s.running());
}

TEST(StudioSession, SetPropertyReachesLiveModule) {
    atp::module_registry registry;
    registry.add<target_module>();
    atp::studio::session s(registry);
    EXPECT_THROW(s.set_property({"target", "limit", "5"}), std::logic_error);
    s.start(make_config(R"({"version": "1.0", "pipeline": {"modules": [{"module": "target"}]}})"));
    s.set_property({"target", "limit", "42"});
    auto* m = s.live_root()->find_module("target");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->properties().at("limit").to_string(), "42");
    s.stop();
    EXPECT_EQ(s.live_root(), nullptr) << "there is no live tree after stop";
}

}  // namespace
