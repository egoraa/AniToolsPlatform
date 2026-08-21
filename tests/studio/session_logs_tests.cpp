// SPDX-License-Identifier: Apache-2.0
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atp/config/node.hpp>
#include <atp/hosting/module_registry.hpp>
#include <atp/module.hpp>
#include <atp/runtime/config_model.hpp>
#include <atp/runtime/config_validator.hpp>
#include <atp/runtime/json_codec.hpp>
#include <atp/studio/module_manager.hpp>
#include <atp/studio/session.hpp>

namespace {

class noisy_module : public atp::module<atp::ports<>, "noisy"> {
   public:
    void initialize(atp::module_context& context) override {
        context.host.warning("noisy is up");
    }
};

atp::runtime::config one_module_config() {
    const atp::config::node proj = atp::runtime::json_parse(R"({
        "version": "3.0",
        "pipeline": {
            "modules": [{"module": "noisy", "name": "talker"}]
        }
    })");
    EXPECT_TRUE(atp::runtime::validate((proj)).empty());
    return atp::runtime::decode(proj);
}

}  // namespace

TEST(StudioSessionLogs, EmptyWithoutAPipeline) {
    atp::module_registry registry;
    atp::studio::session session(registry);

    EXPECT_TRUE(session.collect_logs().empty());
}

TEST(StudioSessionLogs, ARunningModulesLineReachesTheSession) {
    atp::studio::module_manager manager;
    manager.registry().add<noisy_module>();

    atp::studio::session session(manager.registry());
    session.start(one_module_config());

    const std::vector<atp::runtime::log_line> lines = session.collect_logs();
    session.stop();

    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0].path, "talker");
    EXPECT_EQ(lines[0].text, "noisy is up");
    EXPECT_EQ(lines[0].level, atp::log_level::warning);
}

TEST(StudioSessionLogs, TheSecondDrainOfTheSameRunIsEmpty) {
    atp::studio::module_manager manager;
    manager.registry().add<noisy_module>();

    atp::studio::session session(manager.registry());
    session.start(one_module_config());

    EXPECT_EQ(session.collect_logs().size(), 1u);
    EXPECT_TRUE(session.collect_logs().empty());
    session.stop();
}
