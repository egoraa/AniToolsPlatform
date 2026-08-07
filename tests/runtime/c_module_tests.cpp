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

#include <atp/c_module.hpp>
#include <atp/io.hpp>
#include <atp/module.hpp>
#include <atp/module_base.hpp>
#include <atp/module_context.hpp>
#include <atp/module_loader.hpp>
#include <atp/null_host.hpp>
#include <atp/service_directory.hpp>

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

class host_module : public atp::module<atp::io::ports<host_inputs, host_outputs>, "c_test_host"> {};

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
    std::optional<atp::module_loader> loader_;
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
    EXPECT_EQ(loader_->modules(),
              (std::vector<std::pair<std::string, atp::version>>{
                  {"c_probe", atp::version(2, 1)}, {"c_bare", atp::version(1)}, {"c_grown", atp::version(3)}}));
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

/// And the backward half: shorter means a field this host would read is not there at all, which is a
/// refusal rather than a guess. Built here rather than in a fixture plugin because the check runs before
/// anything else in the descriptor is looked at, so nothing else has to be valid.
TEST_F(c_module_test, RefusesADescriptorShorterThanTheHostExpects) {
    atp_module_desc desc{};
    desc.struct_size = static_cast<std::uint32_t>(sizeof(atp_module_desc) - 1);
    desc.name = "c_short";
    try {
        atp::c_module_factory factory{desc};
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
