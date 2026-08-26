// SPDX-License-Identifier: Apache-2.0
#include <cstdint>
#include <deque>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <typeindex>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <atp/hosting/null_host.hpp>
#include <atp/io.hpp>
#include <atp/module.hpp>
#include <atp/module/module_base.hpp>
#include <atp/module/module_context.hpp>
#include <atp/module/service_directory.hpp>
#include <atp/runtime/c_config.hpp>
#include <atp/runtime/c_module.hpp>
#include <atp/runtime/config_binding.hpp>
#include <atp/runtime/module_loader.hpp>
#include <atp/runtime/raw_config.hpp>

namespace {

struct host_inputs : atp::io::inputs {
    atp::io::queued_input<std::int32_t>& i32 = make<atp::io::queued_input<std::int32_t>>("i32");
    atp::io::input<std::int64_t>& i64 = make<atp::io::input<std::int64_t>>("i64");
    atp::io::input<double>& f64 = make<atp::io::input<double>>("f64");
    atp::io::input<bool>& flag = make<atp::io::input<bool>>("flag");
    atp::io::input<std::string>& text = make<atp::io::input<std::string>>("text");
    atp::io::input<atp::io::blob>& bytes = make<atp::io::input<atp::io::blob>>("bytes");
};

struct host_outputs : atp::io::outputs {
    atp::io::output<std::int32_t>& i32 = make<atp::io::output<std::int32_t>>("i32");
    atp::io::output<std::string>& text = make<atp::io::output<std::string>>("text");
    atp::io::output<atp::io::blob>& bytes = make<atp::io::output<atp::io::blob>>("bytes");
};

class host_module : public atp::module<atp::ports<host_inputs, host_outputs>, "c_test_host"> {};

class recording_host final : public atp::module_host {
   public:
    void log(atp::log_level level, std::string_view text) noexcept override {
        lines.emplace_back(level, std::string(text));
    }

    void wake() noexcept override {
        ++wakes;
    }

    std::vector<std::pair<atp::log_level, std::string>> lines;
    int wakes = 0;
};

class c_module_test : public ::testing::Test {
   protected:
    void TearDown() override {
        for (atp::io::output_base* port : host_.outputs().owned()) {
            port->disconnect_all();
        }
        if (module_) {
            for (atp::io::output_base* port : module_->outputs().owned()) {
                port->disconnect_all();
            }
        }
    }

    void load(const char* path) {
        loader_.emplace(path, registry_);
    }

    void create(const std::string& name) {
        module_ = registry_.create(name);
        ASSERT_NE(module_, nullptr);
    }

    void create(const std::string& name, const atp::runtime::config_source& source) {
        atp::config_ptr cfg = registry_.at(name).make_config();
        ASSERT_NE(cfg, nullptr);
        atp::runtime::load_fields_or_throw(*cfg, source);
        module_ = registry_.create(name, std::move(cfg));
        ASSERT_NE(module_, nullptr);
    }

    void feed(atp::io::output_base& source, const std::string& port) {
        source.connect(module_->inputs().at(port));
    }

    void collect(const std::string& port, atp::io::input_base& sink) {
        module_->outputs().at(port).connect(sink);
    }

    void run_lifecycle() {
        module_->initialize(context_);
        module_->start();
    }

    atp::work_status pass() {
        return module_->iterate(stop_.get_token());
    }

    atp::module_registry registry_;
    std::optional<atp::runtime::module_loader> loader_;
    host_module host_;
    atp::module_ptr module_;
    atp::service_directory services_;
    atp::null_host log_host_;
    atp::module_context context_{services_, log_host_};
    std::stop_source stop_;
};

}  // namespace

TEST_F(c_module_test, RegistersEveryDescribedModule) {
    load(ATP_TEST_PLUGIN_C);
    EXPECT_EQ(loader_->modules(), (std::vector<atp::runtime::registered_module>{
                                      {"c_probe", atp::version(2, 1), "c_probe_declared_here.txt"},
                                      {"c_bare", atp::version(1), ""},
                                      {"c_grown", atp::version(3), ""},
                                      {"c_config", atp::version(1), ""},
                                      {"c_config_text", atp::version(1), ""},
                                      {"c_config_bad_path", atp::version(1), ""},
                                      {"c_destroys_taken", atp::version(1), ""},
                                      {"c_declared", atp::version(1), ""}}));
    EXPECT_EQ(registry_.at("c_probe").get_version(), atp::version(2, 1));
}

/// The forward half of the struct_size contract: a plugin built against a later ABI, whose descriptor
/// really is longer than this host's, must load — the host reads only the fields it knows about.
TEST_F(c_module_test, AcceptsADescriptorLongerThanTheHostKnows) {
    load(ATP_TEST_PLUGIN_C);
    create("c_grown");
    EXPECT_EQ(module_->get_name(), "c_grown");
    run_lifecycle();
    EXPECT_EQ(pass(), atp::work_status::idle);
}

/// And the backward half: the floor is the v1 layout, not the host's current sizeof — a descriptor
/// shorter than v1 is missing a field every host reads, which is a refusal rather than a guess. Built
/// here rather than in a fixture plugin because the check runs before anything else in the descriptor
/// is looked at, so nothing else has to be valid.
TEST_F(c_module_test, RefusesADescriptorShorterThanTheHostExpects) {
    atp_module_desc desc{};
    desc.struct_size = static_cast<std::uint32_t>(ATP_MODULE_DESC_SIZE_V1 - 1);
    desc.name = "c_short";
    try {
        atp::runtime::c_module_factory factory{desc};
        FAIL() << "a descriptor missing a field the host reads must not be accepted";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("struct_size"), std::string::npos);
    }
}

TEST_F(c_module_test, UnloadWithdrawsFactories) {
    load(ATP_TEST_PLUGIN_C);
    EXPECT_NE(registry_.find("c_probe"), nullptr);
    loader_.reset();
    EXPECT_EQ(registry_.find("c_probe"), nullptr);
    EXPECT_EQ(registry_.find("c_bare"), nullptr);
}

TEST_F(c_module_test, BuildsDeclaredInputs) {
    load(ATP_TEST_PLUGIN_C);
    create("c_probe");
    EXPECT_EQ(module_->inputs().list().size(), 4u);
    EXPECT_EQ(module_->inputs().at("state_i32").type(), std::type_index(typeid(std::int32_t)));
    EXPECT_EQ(module_->inputs().at("queue_i32").type(), std::type_index(typeid(std::int32_t)));
    EXPECT_EQ(module_->inputs().at("state_text").type(), std::type_index(typeid(std::string)));
    EXPECT_EQ(module_->inputs().at("state_blob").type(), std::type_index(typeid(atp::io::blob)));
}

TEST_F(c_module_test, BuildsDeclaredOutputs) {
    load(ATP_TEST_PLUGIN_C);
    create("c_probe");
    EXPECT_EQ(module_->outputs().list().size(), 6u);
    EXPECT_EQ(module_->outputs().at("out_i32").type(), std::type_index(typeid(std::int32_t)));
    EXPECT_EQ(module_->outputs().at("out_i64").type(), std::type_index(typeid(std::int64_t)));
    EXPECT_EQ(module_->outputs().at("out_f64").type(), std::type_index(typeid(double)));
    EXPECT_EQ(module_->outputs().at("out_bool").type(), std::type_index(typeid(bool)));
    EXPECT_EQ(module_->outputs().at("out_text").type(), std::type_index(typeid(std::string)));
    EXPECT_EQ(module_->outputs().at("out_blob").type(), std::type_index(typeid(atp::io::blob)));
}

TEST_F(c_module_test, BuildsDeclaredProperties) {
    load(ATP_TEST_PLUGIN_C);
    create("c_probe");
    const atp::io::property_base& gain = module_->properties().at("gain");
    EXPECT_EQ(gain.kind(), atp::io::property_kind::number);
    EXPECT_EQ(gain.default_string(), "1.5");
    EXPECT_EQ(gain.to_string(), "1.5");
    EXPECT_TRUE(gain.options().empty());
    EXPECT_TRUE(gain.persistent());

    const atp::io::property_base& mode = module_->properties().at("mode");
    EXPECT_EQ(mode.kind(), atp::io::property_kind::text);
    EXPECT_EQ(mode.options(), (std::vector<std::string>{"plain", "verbose"}));

    const atp::io::property_base& count = module_->properties().at("count");
    EXPECT_EQ(count.kind(), atp::io::property_kind::number);
    EXPECT_EQ(count.options(), (std::vector<std::string>{"1", "2", "3"}));
    EXPECT_EQ(count.default_string(), "3");

    EXPECT_EQ(module_->properties().at("flag").kind(), atp::io::property_kind::boolean);
    EXPECT_FALSE(module_->properties().at("flag").persistent());
}

TEST_F(c_module_test, RejectsPropertyValueOutsideOptions) {
    load(ATP_TEST_PLUGIN_C);
    create("c_probe");
    EXPECT_THROW(module_->properties().at("count").from_string("4"), std::invalid_argument);
    EXPECT_EQ(module_->properties().at("count").to_string(), "3");
}

TEST_F(c_module_test, QueueInputHonoursDeclaredLimit) {
    load(ATP_TEST_PLUGIN_C);
    create("c_probe");
    EXPECT_EQ(module_->inputs().at("queue_i32").stats().capacity, 2u);

    feed(host_.outputs().i32, "queue_i32");
    host_.outputs().i32(1);
    host_.outputs().i32(2);
    host_.outputs().i32(3);

    const atp::io::input_stats stats = module_->inputs().at("queue_i32").stats();
    EXPECT_EQ(stats.received, 3u);
    EXPECT_EQ(stats.discarded, 1u);
    EXPECT_EQ(stats.pending, 2u);
}

TEST_F(c_module_test, DrainsQueueIntoOutput) {
    load(ATP_TEST_PLUGIN_C);
    create("c_probe");
    feed(host_.outputs().i32, "queue_i32");
    collect("out_i32", host_.inputs().i32);
    run_lifecycle();

    host_.outputs().i32(11);
    host_.outputs().i32(12);
    EXPECT_EQ(pass(), atp::work_status::busy);
    EXPECT_EQ(host_.inputs().i32.drain(), (std::deque<std::int32_t>{11, 12}));
    EXPECT_EQ(pass(), atp::work_status::idle);
}

TEST_F(c_module_test, ScalesStateInputByProperty) {
    load(ATP_TEST_PLUGIN_C);
    create("c_probe");
    feed(host_.outputs().i32, "state_i32");
    collect("out_i64", host_.inputs().i64);
    module_->properties().at("count").from_string("2");
    run_lifecycle();

    host_.outputs().i32(21);
    EXPECT_EQ(pass(), atp::work_status::busy);
    EXPECT_EQ(host_.inputs().i64.get(), 42);
}

TEST_F(c_module_test, CarriesTextAcrossTheBoundary) {
    load(ATP_TEST_PLUGIN_C);
    create("c_probe");
    feed(host_.outputs().text, "state_text");
    collect("out_text", host_.inputs().text);
    run_lifecycle();

    const std::string sent = "a string long enough to live on the heap rather than inside the object";
    host_.outputs().text(sent);
    EXPECT_EQ(pass(), atp::work_status::busy);
    EXPECT_EQ(host_.inputs().text.get(), sent);
}

TEST_F(c_module_test, CarriesBlobAcrossTheBoundary) {
    load(ATP_TEST_PLUGIN_C);
    create("c_probe");
    feed(host_.outputs().bytes, "state_blob");
    collect("out_blob", host_.inputs().bytes);
    run_lifecycle();

    const atp::io::blob sent{std::byte{0x00}, std::byte{0xff}, std::byte{0x7f}};
    host_.outputs().bytes(sent);
    EXPECT_EQ(pass(), atp::work_status::busy);
    EXPECT_EQ(host_.inputs().bytes.get(), sent);
}

TEST_F(c_module_test, PropertyEditReachesModule) {
    load(ATP_TEST_PLUGIN_C);
    create("c_probe");
    collect("out_f64", host_.inputs().f64);
    run_lifecycle();

    EXPECT_EQ(pass(), atp::work_status::idle);
    module_->properties().at("gain").from_string("2.25");
    EXPECT_EQ(pass(), atp::work_status::busy);
    EXPECT_EQ(host_.inputs().f64.get(), 2.25);
    EXPECT_EQ(pass(), atp::work_status::idle);
}

TEST_F(c_module_test, FailedPassBecomesAnException) {
    load(ATP_TEST_PLUGIN_C);
    create("c_probe");
    run_lifecycle();
    module_->properties().at("fail").from_string("true");
    try {
        pass();
        FAIL() << "a failed pass must not go unnoticed";
    } catch (const std::runtime_error& e) {
        const std::string text = e.what();
        EXPECT_NE(text.find("asked to fail"), std::string::npos);
        EXPECT_NE(text.find("c_probe"), std::string::npos);
    }
}

TEST_F(c_module_test, StopTokenReachesModule) {
    load(ATP_TEST_PLUGIN_C);
    create("c_probe");
    feed(host_.outputs().i32, "queue_i32");
    run_lifecycle();

    host_.outputs().i32(1);
    stop_.request_stop();
    EXPECT_EQ(pass(), atp::work_status::idle);
    EXPECT_EQ(module_->inputs().at("queue_i32").stats().pending, 1u);
}

TEST_F(c_module_test, LogsThroughItsOwnHostOnceTheContextIsGone) {
    load(ATP_TEST_PLUGIN_C);
    create("c_probe");
    recording_host mine;
    {
        atp::service_directory services;
        atp::module_context scoped{services, mine};
        module_->initialize(scoped);
    }
    module_->start();

    ASSERT_EQ(mine.lines.size(), 2u);
    EXPECT_EQ(mine.lines[0], std::make_pair(atp::log_level::debug, std::string("initialized")));
    EXPECT_EQ(mine.lines[1], std::make_pair(atp::log_level::info, std::string("started")));
}

TEST_F(c_module_test, StopIsCorrectAfterInitializeWithoutStart) {
    load(ATP_TEST_PLUGIN_C);
    create("c_probe");
    module_->initialize(context_);
    EXPECT_NO_THROW(module_->stop());
}

TEST_F(c_module_test, ModuleWithoutPortsIsLegal) {
    load(ATP_TEST_PLUGIN_C);
    create("c_bare");
    EXPECT_TRUE(module_->inputs().list().empty());
    EXPECT_TRUE(module_->outputs().list().empty());
    EXPECT_TRUE(module_->properties().list().empty());
    run_lifecycle();
    EXPECT_EQ(pass(), atp::work_status::idle);
}

TEST_F(c_module_test, RefusesAnotherCAbiVersion) {
    try {
        load(ATP_TEST_PLUGIN_C_BAD_ABI);
        FAIL() << "a plugin of another C ABI must not load";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("C ABI"), std::string::npos);
    }
    EXPECT_EQ(registry_.find("c_future"), nullptr);
}

TEST_F(c_module_test, MalformedDescriptorWithdrawsTheSoundOnes) {
    EXPECT_THROW(load(ATP_TEST_PLUGIN_C_BAD_DESC), std::runtime_error);
    EXPECT_EQ(registry_.find("c_sound"), nullptr);
    EXPECT_EQ(registry_.find("c_broken"), nullptr);
}

TEST_F(c_module_test, ConfigIsReadableThroughTheAccessors) {
    load(ATP_TEST_PLUGIN_C);
    create("c_config", {atp::config::node::object({
                            {"channels", atp::config::node::array({1, 2, 6})},
                            {"name", "rig"},
                        }),
                        {},
                        {},
                        false});
    collect("report", host_.inputs().text);
    run_lifecycle();

    EXPECT_EQ(pass(), atp::work_status::busy);
    EXPECT_EQ(host_.inputs().text.get(),
              "root=ok kind=object size=2 key0=channels find=ok find-not-root channels=array third=6 "
              "object-read=no absent=none oob=none name=rig");
}

TEST_F(c_module_test, ConfigRootIsNullWhenNoneWasGiven) {
    load(ATP_TEST_PLUGIN_C);
    create("c_config");
    collect("report", host_.inputs().text);
    run_lifecycle();

    EXPECT_EQ(pass(), atp::work_status::busy);
    const std::string report = host_.inputs().text.get();
    EXPECT_NE(report.find("root=ok"), std::string::npos);
    EXPECT_NE(report.find("kind=other"), std::string::npos);
    EXPECT_NE(report.find("size=0"), std::string::npos);
    EXPECT_NE(report.find("object-read=no"), std::string::npos);
}

TEST_F(c_module_test, OpaqueConfigTextAndOriginReachAForeignModule) {
    load(ATP_TEST_PLUGIN_C);
    create("c_config_text", {{}, "rate = 48000\n", "rig.ini", true});
    collect("report", host_.inputs().text);
    run_lifecycle();

    EXPECT_EQ(pass(), atp::work_status::busy);
    EXPECT_EQ(host_.inputs().text.get(), "text-len=13 origin=rig.ini opaque=1 rate=none");
}

TEST_F(c_module_test, APathFindsANodeAndAConfigWithoutAFileHasNoText) {
    load(ATP_TEST_PLUGIN_C);
    create("c_config_text",
           {atp::config::node::object({{"audio", atp::config::node::object({{"rate", 48000}})}}), {}, {}, false});
    collect("report", host_.inputs().text);
    run_lifecycle();

    EXPECT_EQ(pass(), atp::work_status::busy);
    EXPECT_EQ(host_.inputs().text.get(), "text=none origin=none opaque=0 rate=48000");
}

TEST_F(c_module_test, AHostErrorRaisedInsideCreateSurfacesFromCreate) {
    load(ATP_TEST_PLUGIN_C);
    try {
        module_ = registry_.create("c_config_bad_path");
        FAIL() << "a malformed path from inside create must not be swallowed";
    } catch (const atp::config::access_error& e) {
        EXPECT_NE(std::string(e.what()).find("bad path"), std::string::npos);
    }
}

TEST_F(c_module_test, AFailedCreateStillDestroysWhatThePluginBuilt) {
    load(ATP_TEST_PLUGIN_C);
    module_ = registry_.create("c_destroys_taken");
    EXPECT_THROW(module_ = registry_.create("c_config_bad_path"), atp::config::access_error);

    create("c_destroys_taken");
    collect("report", host_.inputs().text);
    run_lifecycle();

    EXPECT_EQ(pass(), atp::work_status::busy);
    EXPECT_EQ(host_.inputs().text.get(), "destroys=1");
}

TEST_F(c_module_test, AParsedFileCarriesTextAndTreeAtOnce) {
    load(ATP_TEST_PLUGIN_C);
    create("c_config_text", {atp::config::node::object({{"audio", atp::config::node::object({{"rate", 48000}})}}),
                             "{\"audio\":{\"rate\":48000}}", "rig.json", false});
    collect("report", host_.inputs().text);
    run_lifecycle();

    EXPECT_EQ(pass(), atp::work_status::busy);
    EXPECT_EQ(host_.inputs().text.get(), "text-len=24 origin=rig.json opaque=0 rate=48000");
}

TEST_F(c_module_test, DeclarationAnswersFromDescriptorsWithoutCreating) {
    load(ATP_TEST_PLUGIN_C);
    const atp::module_declaration decl = registry_.at("c_probe").declaration();

    EXPECT_FALSE(decl.inputs.empty());
    EXPECT_FALSE(decl.outputs.empty());
    const atp::module_ptr probe = registry_.create("c_probe");
    ASSERT_EQ(decl.inputs.size(), probe->inputs().owned().size());
    for (std::size_t i = 0; i < decl.inputs.size(); ++i) {
        EXPECT_EQ(decl.inputs[i].name, probe->inputs().owned()[i]->name());
        EXPECT_EQ(decl.inputs[i].type, probe->inputs().owned()[i]->type());
    }
    ASSERT_EQ(decl.outputs.size(), probe->outputs().owned().size());
    for (std::size_t i = 0; i < decl.outputs.size(); ++i) {
        EXPECT_EQ(decl.outputs[i].name, probe->outputs().owned()[i]->name());
        EXPECT_EQ(decl.outputs[i].type, probe->outputs().owned()[i]->type());
    }
    ASSERT_EQ(decl.properties.size(), probe->properties().owned().size());
    for (std::size_t i = 0; i < decl.properties.size(); ++i) {
        EXPECT_EQ(decl.properties[i].name, probe->properties().owned()[i]->name());
        EXPECT_EQ(decl.properties[i].default_value, probe->properties().owned()[i]->default_string());
        EXPECT_EQ(decl.properties[i].options, probe->properties().owned()[i]->options());
        EXPECT_EQ(decl.properties[i].persistent, probe->properties().owned()[i]->persistent());
    }
}

namespace {

bool c_create_was_called = false;

void* counting_create(const atp_api*, atp_ctx*, void*) {
    c_create_was_called = true;
    return nullptr;
}
void counting_destroy(void*) {}
atp_work counting_iterate(void*) {
    return ATP_WORK_IDLE;
}

void fill_callbacks(atp_module_desc& desc) {
    desc.create = &counting_create;
    desc.destroy = &counting_destroy;
    desc.iterate = &counting_iterate;
}

}  // namespace

TEST_F(c_module_test, DeclarationNeverReachesForTheCreateCallback) {
    const atp_input_desc inputs[] = {{"step", ATP_KIND_I32, ATP_STATE, 0, ATP_DROP_OLDEST}};
    const atp_output_desc outputs[] = {{"count", ATP_KIND_I32}};
    atp_module_desc desc{};
    desc.struct_size = static_cast<std::uint32_t>(sizeof(atp_module_desc));
    desc.name = "c_never_created";
    desc.inputs = inputs;
    desc.input_count = 1;
    desc.outputs = outputs;
    desc.output_count = 1;
    fill_callbacks(desc);
    c_create_was_called = false;

    const atp::runtime::c_module_factory factory{desc};
    const atp::module_declaration decl = factory.declaration();

    ASSERT_EQ(decl.inputs.size(), 1u);
    EXPECT_EQ(decl.inputs[0].name, "step");
    EXPECT_EQ(decl.inputs[0].type, std::type_index(typeid(std::int32_t)));
    ASSERT_EQ(decl.outputs.size(), 1u);
    EXPECT_EQ(decl.outputs[0].name, "count");
    EXPECT_FALSE(c_create_was_called);
}

TEST_F(c_module_test, ABlobPropertyIsRefusedBeforeAFactoryForItExists) {
    const atp_property_desc properties[] = {{"payload", ATP_KIND_BLOB, "", nullptr, 0, 1}};
    atp_module_desc desc{};
    desc.struct_size = static_cast<std::uint32_t>(sizeof(atp_module_desc));
    desc.name = "c_blob_property";
    fill_callbacks(desc);
    desc.properties = properties;
    desc.property_count = 1;

    try {
        const atp::runtime::c_module_factory factory{desc};
        FAIL() << "a blob property has no codec, so the descriptor is refused rather than described";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("is a blob"), std::string::npos);
    }
}

TEST_F(c_module_test, DeclarationRefusesAnUnparsableDefaultTheSameWayCreationDoes) {
    const atp_property_desc properties[] = {{"limit", ATP_KIND_I32, "not a number", nullptr, 0, 1}};
    atp_module_desc desc{};
    desc.struct_size = static_cast<std::uint32_t>(sizeof(atp_module_desc));
    desc.name = "c_bad_default";
    fill_callbacks(desc);
    desc.properties = properties;
    desc.property_count = 1;

    const atp::runtime::c_module_factory factory{desc};
    try {
        (void)factory.declaration();
        FAIL() << "a default that cannot be parsed makes the module unusable and must not describe as healthy";
    } catch (const std::invalid_argument& e) {
        EXPECT_NE(std::string(e.what()).find("unparsable default"), std::string::npos);
    }
}

TEST_F(c_module_test, AModuleDeclaringFieldsGetsAConfigThatCarriesThem) {
    load(ATP_TEST_PLUGIN_C);
    const atp::config_ptr cfg = registry_.at("c_declared").make_config();

    ASSERT_NE(cfg, nullptr);
    EXPECT_EQ(dynamic_cast<const atp::runtime::raw_config*>(cfg.get()), nullptr);
    ASSERT_EQ(cfg->entries().size(), 5U);
    EXPECT_EQ(cfg->entries()[0].name(), "rate");
    EXPECT_TRUE(cfg->entries()[0].required());
    EXPECT_EQ(cfg->find("engine")->options(), (std::vector<std::string>{"fm", "additive"}));
    EXPECT_EQ(cfg->find("master")->group().find("gain")->value<double>(), 1.0);
    EXPECT_EQ(cfg->find("voices")->element_shape().entries()[0].name(), "note");
}

TEST_F(c_module_test, AModuleDeclaringNoFieldsStillGetsTheDocumentWhole) {
    load(ATP_TEST_PLUGIN_C);
    const atp::config_ptr cfg = registry_.at("c_config").make_config();

    EXPECT_NE(dynamic_cast<const atp::runtime::raw_config*>(cfg.get()), nullptr);
    EXPECT_TRUE(cfg->entries().empty());
}

TEST_F(c_module_test, TheModuleReadsDefaultsForKeysTheDocumentDidNotWrite) {
    load(ATP_TEST_PLUGIN_C);
    create("c_declared", {atp::config::node::object({{"rate", 48000}}), {}, {}, false});
    collect("report", host_.inputs().text);
    run_lifecycle();

    EXPECT_EQ(pass(), atp::work_status::busy);
    const std::string report = host_.inputs().text.get();
    EXPECT_NE(report.find("rate=yes"), std::string::npos);
    EXPECT_NE(report.find("engine=yes"), std::string::npos) << "a default the document never wrote";
    EXPECT_NE(report.find("gain=yes"), std::string::npos) << "a default inside a nested object";
    EXPECT_NE(report.find("taps=yes"), std::string::npos) << "an array nobody grew is still an array";
    EXPECT_NE(report.find("absent=no"), std::string::npos) << "an undeclared key is nowhere to be found";
}

TEST_F(c_module_test, ARequiredFieldTheDocumentOmitsIsAProblemNamingTheFile) {
    load(ATP_TEST_PLUGIN_C);
    const atp::config_ptr cfg = registry_.at("c_declared").make_config();

    try {
        atp::runtime::load_fields_or_throw(*cfg, {atp::config::node::object({}), {}, "rig.json", false});
        FAIL() << "expected a config_error";
    } catch (const std::exception& e) {
        const std::string what = e.what();
        EXPECT_NE(what.find("rig.json"), std::string::npos);
        EXPECT_NE(what.find("rate"), std::string::npos);
    }
}

TEST_F(c_module_test, AnOpaqueFileLeavesEveryFieldAtItsDefaultAndStillDeliversTheText) {
    load(ATP_TEST_PLUGIN_C);
    const atp::config_ptr cfg = registry_.at("c_declared").make_config();
    (void)atp::runtime::load_fields(*cfg, {atp::config::node(), "rate = 3", "rig.ini", true});

    EXPECT_EQ(cfg->text(), "rate = 3");
    EXPECT_EQ(cfg->origin(), "rig.ini");
    EXPECT_TRUE(cfg->is_opaque());
    EXPECT_EQ(cfg->find("engine")->value<std::string>(), "fm");
}
