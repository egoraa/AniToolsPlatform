// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <typeindex>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <atp/io.hpp>
#include <atp/module.hpp>
#include <atp/module_base.hpp>
#include <atp/module_context.hpp>
#include <atp/module_loader.hpp>
#include <atp/module_registry.hpp>
#include <atp/null_host.hpp>
#include <atp/service_directory.hpp>
#include <atp/studio/module_manager.hpp>
#include <atp/version.hpp>

namespace {

struct probe_inputs : atp::io::inputs {
    atp::io::input<std::int32_t>& i32 = make<atp::io::input<std::int32_t>>("i32");
    atp::io::input<std::int64_t>& i64 = make<atp::io::input<std::int64_t>>("i64");
    atp::io::input<double>& f64 = make<atp::io::input<double>>("f64");
    atp::io::input<bool>& flag = make<atp::io::input<bool>>("flag");
    atp::io::input<std::string>& text = make<atp::io::input<std::string>>("text");
    atp::io::input<atp::io::blob>& bytes = make<atp::io::input<atp::io::blob>>("bytes");
};

struct probe_outputs : atp::io::outputs {
    atp::io::output<std::int32_t>& i32 = make<atp::io::output<std::int32_t>>("i32");
    atp::io::output<std::int64_t>& i64 = make<atp::io::output<std::int64_t>>("i64");
    atp::io::output<double>& f64 = make<atp::io::output<double>>("f64");
    atp::io::output<bool>& flag = make<atp::io::output<bool>>("flag");
    atp::io::output<std::string>& text = make<atp::io::output<std::string>>("text");
    atp::io::output<atp::io::blob>& bytes = make<atp::io::output<atp::io::blob>>("bytes");
};

class probe_module : public atp::module<atp::io::ports<probe_inputs, probe_outputs>, "py_test_host"> {};

class python_bridge_test : public ::testing::Test {
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

    void scan(const char* dir) {
#ifdef _WIN32
        _putenv_s("ATP_PYTHON_PATH", dir);
#else
        setenv("ATP_PYTHON_PATH", dir, 1);
#endif
    }

    void load() {
        loader_.emplace(ATP_PYTHON_BRIDGE, registry_);
    }

    [[nodiscard]] bool registered(const std::string& name, atp::version expected) const {
        const std::vector<std::pair<std::string, atp::version>>& all = loader_->modules();
        return std::find(all.begin(), all.end(), std::pair{name, expected}) != all.end();
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
    probe_module host_;
    atp::module_ptr module_;
    atp::service_directory services_;
    atp::null_host log_host_;
    atp::module_context context_{services_, log_host_};
    std::stop_source stop_;
};

}  // namespace

TEST_F(python_bridge_test, LoadsWithNoModulesWhenNothingIsScanned) {
    scan("");
    load();
    EXPECT_TRUE(loader_->modules().empty());
}

TEST_F(python_bridge_test, RegistersAModuleDeclaredByAScript) {
    scan(ATP_PYTHON_BRIDGE_SCRIPTS);
    load();
    EXPECT_TRUE(registered("py_declares", atp::version(2, 1)));
    EXPECT_TRUE(registered("py_echo", atp::version(1, 0)));
    EXPECT_EQ(registry_.at("py_declares").get_version(), atp::version(2, 1));
}

TEST_F(python_bridge_test, BuildsPlatformPortsFromTheClassBody) {
    scan(ATP_PYTHON_BRIDGE_SCRIPTS);
    load();
    create("py_declares");
    EXPECT_EQ(module_->inputs().list().size(), 3u);
    EXPECT_EQ(module_->inputs().at("state_i32").type(), std::type_index(typeid(std::int32_t)));
    EXPECT_EQ(module_->inputs().at("queue_i32").type(), std::type_index(typeid(std::int32_t)));
    EXPECT_EQ(module_->inputs().at("in_text").type(), std::type_index(typeid(std::string)));
    EXPECT_EQ(module_->outputs().list().size(), 3u);
    EXPECT_EQ(module_->outputs().at("out_i64").type(), std::type_index(typeid(std::int64_t)));
    EXPECT_EQ(module_->outputs().at("out_f64").type(), std::type_index(typeid(double)));
    EXPECT_EQ(module_->outputs().at("out_blob").type(), std::type_index(typeid(atp::io::blob)));
}

TEST_F(python_bridge_test, BuildsPropertiesWithDefaultsAndOptions) {
    scan(ATP_PYTHON_BRIDGE_SCRIPTS);
    load();
    create("py_declares");
    EXPECT_EQ(module_->properties().at("gain").default_string(), "1.5");
    EXPECT_EQ(module_->properties().at("mode").options(), (std::vector<std::string>{"plain", "verbose"}));
    EXPECT_TRUE(module_->properties().at("gain").persistent());
    EXPECT_FALSE(module_->properties().at("transient").persistent());
}

TEST_F(python_bridge_test, CarriesEveryKindInBothDirections) {
    scan(ATP_PYTHON_BRIDGE_SCRIPTS);
    load();
    create("py_echo");
    feed(host_.outputs().i32, "in_i32");
    feed(host_.outputs().i64, "in_i64");
    feed(host_.outputs().f64, "in_f64");
    feed(host_.outputs().flag, "in_bool");
    feed(host_.outputs().text, "in_text");
    feed(host_.outputs().bytes, "in_blob");
    collect("out_i32", host_.inputs().i32);
    collect("out_i64", host_.inputs().i64);
    collect("out_f64", host_.inputs().f64);
    collect("out_bool", host_.inputs().flag);
    collect("out_text", host_.inputs().text);
    collect("out_blob", host_.inputs().bytes);

    host_.outputs().i32(7);
    host_.outputs().i64(1LL << 40);
    host_.outputs().f64(0.25);
    host_.outputs().flag(true);
    host_.outputs().text(std::string("hi"));
    host_.outputs().bytes(atp::io::blob{std::byte{1}, std::byte{2}});

    run_lifecycle();
    EXPECT_EQ(pass(), atp::work_status::busy);

    EXPECT_EQ(host_.inputs().i32.get(), 7);
    EXPECT_EQ(host_.inputs().i64.get(), 1LL << 40);
    EXPECT_EQ(host_.inputs().f64.get(), 0.25);
    EXPECT_EQ(host_.inputs().flag.get(), true);
    EXPECT_EQ(host_.inputs().text.get(), "hi!");
    EXPECT_EQ(host_.inputs().bytes.get(), (atp::io::blob{std::byte{2}, std::byte{1}}));
}

TEST_F(python_bridge_test, ReadsAPropertyThroughTheBoundary) {
    scan(ATP_PYTHON_BRIDGE_SCRIPTS);
    load();
    create("py_echo");
    feed(host_.outputs().i32, "in_i32");
    collect("out_gain", host_.inputs().f64);
    module_->properties().at("gain").from_string("2.5");
    host_.outputs().i32(1);
    run_lifecycle();
    EXPECT_EQ(pass(), atp::work_status::busy);
    EXPECT_EQ(host_.inputs().f64.get(), 2.5);
}

TEST_F(python_bridge_test, RefusesAValueTooLargeForItsPort) {
    scan(ATP_PYTHON_BRIDGE_SCRIPTS);
    load();
    create("py_echo");
    feed(host_.outputs().i32, "in_i32");
    host_.outputs().i32(1);
    run_lifecycle();
    module_->properties().at("overflow").from_string("true");
    try {
        pass();
        FAIL() << "writing 2^31 to an i32 port must not be silently truncated";
    } catch (const std::runtime_error& error) {
        const std::string text = error.what();
        EXPECT_NE(text.find("py_echo"), std::string::npos);
        EXPECT_NE(text.find("OverflowError"), std::string::npos);
    }
}

TEST_F(python_bridge_test, ReportsAnExceptionAsAModuleError) {
    scan(ATP_PYTHON_BRIDGE_SCRIPTS);
    load();
    create("py_raises");
    run_lifecycle();
    try {
        pass();
        FAIL() << "an exception in iterate must stop the pipeline";
    } catch (const std::runtime_error& error) {
        const std::string text = error.what();
        EXPECT_NE(text.find("py_raises"), std::string::npos);
        EXPECT_NE(text.find("the script said no"), std::string::npos);
        EXPECT_NE(text.find("t_raises.py"), std::string::npos);
    }
}

TEST_F(python_bridge_test, ReachesTheStudioPaletteThroughAScannedDirectory) {
    scan(ATP_PYTHON_BRIDGE_SCRIPTS);
    atp::studio::module_manager manager;
    manager.add_search_dir(std::filesystem::path(ATP_PYTHON_BRIDGE).parent_path());
    manager.rescan();

    const atp::studio::module_info* found = nullptr;
    for (const atp::studio::plugin_info& plugin : manager.plugins()) {
        for (const atp::studio::module_info& module : plugin.modules) {
            if (module.name == "py_declares") {
                found = &module;
            }
        }
    }
    ASSERT_NE(found, nullptr) << "the palette never saw a module declared by a Python script";
    EXPECT_FALSE(found->broken) << found->error;
    EXPECT_EQ(found->inputs.size(), 3u);
    EXPECT_EQ(found->outputs.size(), 3u);
    EXPECT_EQ(found->properties.size(), 3u);
}

TEST_F(python_bridge_test, SkipsABrokenScriptAndKeepsItsNeighbour) {
    scan(ATP_PYTHON_BRIDGE_SCRIPTS_BROKEN);
    load();
    EXPECT_EQ(loader_->modules().size(), 1u);
    EXPECT_NE(registry_.find("py_neighbour"), nullptr);
}
