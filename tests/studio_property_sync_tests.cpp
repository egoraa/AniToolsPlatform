#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atp/module.hpp>
#include <atp/module_registry.hpp>
#include <atp/pipeline.hpp>
#include <atp/pipeline_runner.hpp>
#include <atp/runtime/pipeline_builder.hpp>
#include <atp/studio/document.hpp>
#include <atp/studio/property_sync.hpp>

namespace {

struct sync_props : atp::io::properties {
    atp::io::property<int>& limit = make<atp::io::property<int>>("limit", 10);
    atp::io::property<bool>& verbose = make<atp::io::property<bool>>("verbose", false);
    atp::io::property<std::string>& scratch = make<atp::io::property<std::string>>("scratch", "", atp::io::transient);
};
class sync_module : public atp::module<atp::io::ports<atp::io::inputs, atp::io::outputs, sync_props>, "syncer"> {};

TEST(StudioPropertySync, PullsPersistentValuesIntoDocument) {
    atp::module_registry registry;
    registry.add<sync_module>();
    auto doc = atp::studio::document::create();
    doc.add_module("", "syncer");
    doc.set_property("", "syncer", "limit", 999);  // goes stale once edited on the fly

    atp::pipeline pipe;
    atp::pipeline_runner runner;
    atp::runtime::build_pipeline(pipe, runner, doc.config(), registry);

    auto* m = pipe.root().find_module("syncer");
    ASSERT_NE(m, nullptr);
    m->properties().at("limit").from_string("42");  // edited on the fly
    m->properties().at("verbose").from_string("true");
    m->properties().at("scratch").from_string("tmp");  // transient — never reaches the document

    atp::studio::sync_persistent_properties(doc, doc.config(), pipe.root());

    const auto& props = doc.config().pipeline.modules[0].module->properties;
    ASSERT_EQ(props.size(), 2u);
    // a number stayed a number and a bool a bool: the types were restored from the kind
    EXPECT_EQ(props[0].second, nlohmann::json(42));
    EXPECT_EQ(props[1].second, nlohmann::json(true));
}

TEST(StudioPropertySync, DefaultValuesAreDroppedFromDocument) {
    atp::module_registry registry;
    registry.add<sync_module>();
    auto doc = atp::studio::document::create();
    doc.add_module("", "syncer");
    doc.set_property("", "syncer", "limit", 42);

    atp::pipeline pipe;
    atp::pipeline_runner runner;
    atp::runtime::build_pipeline(pipe, runner, doc.config(), registry);
    pipe.root().find_module("syncer")->properties().at("limit").from_string("10");  // back to the default

    atp::studio::sync_persistent_properties(doc, doc.config(), pipe.root());
    EXPECT_TRUE(doc.config().pipeline.modules[0].module->properties.empty());
}

}  // namespace
