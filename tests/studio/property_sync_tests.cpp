#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atp/module.hpp>
#include <atp/module_registry.hpp>
#include <atp/pipeline.hpp>
#include <atp/pipeline_runner.hpp>
#include <atp/runtime/pipeline_builder.hpp>
#include <atp/studio/project.hpp>
#include <atp/studio/property_sync.hpp>

#include "support/required.hpp"

namespace {

struct sync_props : atp::io::properties {
    atp::io::property<int>& limit = make<atp::io::property<int>>("limit", 10);
    atp::io::property<bool>& verbose = make<atp::io::property<bool>>("verbose", false);
    atp::io::property<std::string>& scratch = make<atp::io::property<std::string>>("scratch", "", atp::io::transient);
};
class sync_module : public atp::module<atp::io::ports<atp::io::inputs, atp::io::outputs, sync_props>, "syncer"> {};

TEST(StudioPropertySync, PullsPersistentValuesIntoProject) {
    atp::module_registry registry;
    registry.add<sync_module>();
    auto proj = atp::studio::project::create();
    proj.add_module("", "syncer");
    proj.set_property("", "syncer", "limit", 999);

    atp::pipeline pipe;
    atp::pipeline_runner runner;
    atp::runtime::build_pipeline(pipe, runner, proj.config(), registry);

    auto* m = pipe.root().find_module("syncer");
    ASSERT_NE(m, nullptr);
    m->properties().at("limit").from_string("42");
    m->properties().at("verbose").from_string("true");
    m->properties().at("scratch").from_string("tmp");

    atp::studio::sync_persistent_properties(proj, proj.config(), pipe.root());

    const auto& props = atp_tests::required(proj.config().pipeline.modules[0].module).properties;
    ASSERT_EQ(props.size(), 2u);
    EXPECT_EQ(props[0].second, nlohmann::json(42));
    EXPECT_EQ(props[1].second, nlohmann::json(true));
}

TEST(StudioPropertySync, DefaultValuesAreDroppedFromProject) {
    atp::module_registry registry;
    registry.add<sync_module>();
    auto proj = atp::studio::project::create();
    proj.add_module("", "syncer");
    proj.set_property("", "syncer", "limit", 42);

    atp::pipeline pipe;
    atp::pipeline_runner runner;
    atp::runtime::build_pipeline(pipe, runner, proj.config(), registry);
    pipe.root().find_module("syncer")->properties().at("limit").from_string("10");

    atp::studio::sync_persistent_properties(proj, proj.config(), pipe.root());
    EXPECT_TRUE(atp_tests::required(proj.config().pipeline.modules[0].module).properties.empty());
}

}  // namespace
