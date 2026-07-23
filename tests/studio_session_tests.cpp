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

class studio_source : public atp::module<atp::io::inputs, feed_outputs, "studio_source"> {
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

class studio_sink : public atp::module<drain_inputs, atp::io::outputs, "studio_sink"> {
   public:
    atp::work_status iterate(std::stop_token) override {
        return inputs().value.try_pop() ? atp::work_status::busy : atp::work_status::idle;
    }
};

atp::runtime::config session_config() {
    const nlohmann::json doc = nlohmann::json::parse(R"({
        "version": "1.0",
        "pipeline": {
            "children": [
                {"group": "left", "children": [{"module": "studio_source", "name": "src"}],
                 "expose": {"outputs": {"out": "src.value"}}},
                {"group": "right", "children": [{"module": "studio_sink", "name": "sink"}],
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

    // ждём прохождения значения по единственной связи корня (таймаут щедрый)
    std::optional<std::string> seen;
    for (int i = 0; i < 500 && !seen; ++i) {
        for (const auto& sample : s.sample_connections()) {
            if (sample.writes > 0 && sample.value) {
                EXPECT_EQ(sample.group_path, "");  // соединение объявлено в корне
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
    s.start(cfg);  // повторный запуск — свежий пайплайн
    s.stop();
}

TEST(StudioSession, BuildFailureLeavesSessionClean) {
    atp::studio::module_manager manager;
    atp::studio::session s(manager.registry());

    const atp::runtime::config cfg = session_config();  // модули не зарегистрированы
    EXPECT_THROW(s.start(cfg), atp::runtime::config_error);
    EXPECT_FALSE(s.running());
    EXPECT_TRUE(s.sample_connections().empty());

    manager.registry().add<studio_source>();
    manager.registry().add<studio_sink>();
    s.start(cfg);  // сессия пригодна после отказа
    s.stop();
}

}  // namespace
