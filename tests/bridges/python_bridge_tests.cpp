// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <typeindex>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <atp/hosting/module_registry.hpp>
#include <atp/io.hpp>
#include <atp/module.hpp>
#include <atp/module/module_base.hpp>
#include <atp/module/module_context.hpp>
#include <atp/module/module_host.hpp>
#include <atp/module/service_directory.hpp>
#include <atp/runtime/config_binding.hpp>
#include <atp/runtime/module_loader.hpp>
#include <atp/studio/languages.hpp>
#include <atp/studio/module_manager.hpp>
#include <atp/studio/script_modules.hpp>
#include <atp/support/version.hpp>

namespace {

std::filesystem::path unicode_name(std::string_view prefix,
                                   std::initializer_list<char32_t> points,
                                   std::string_view suffix) {
    std::u32string name;
    for (const char c : prefix) {
        name.push_back(static_cast<char32_t>(c));
    }
    name.append(points);
    for (const char c : suffix) {
        name.push_back(static_cast<char32_t>(c));
    }
    return {name};
}

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

class probe_module : public atp::module<atp::ports<probe_inputs, probe_outputs>, "py_test_host"> {};

class recording_host final : public atp::module_host {
   public:
    void log(atp::log_level level, std::string_view text) noexcept override {
        lines.emplace_back(level, std::string(text));
    }

    void wake() noexcept override {}

    std::vector<std::pair<atp::log_level, std::string>> lines;
};

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
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        setenv("ATP_PYTHON_PATH", dir, 1);
#endif
    }

    void load() {
        loader_.emplace(ATP_PYTHON_BRIDGE, registry_);
    }

    [[nodiscard]] bool registered(const std::string& name, atp::version expected) const {
        return std::ranges::any_of(loader_->modules(), [&name, expected](const atp::runtime::registered_module& m) {
            return m.name == name && m.ver == expected;
        });
    }

    [[nodiscard]] std::string source_of(const std::string& name) const {
        for (const atp::runtime::registered_module& m : loader_->modules()) {
            if (m.name == name) {
                return m.source;
            }
        }
        return {};
    }

    [[nodiscard]] static std::filesystem::path own_scripts_dir() {
        return std::filesystem::path(ATP_PYTHON_BRIDGE).parent_path() / "python";
    }

    [[nodiscard]] std::size_t registered_from(const std::filesystem::path& dir) const {
        std::error_code ec;
        const std::filesystem::path root = std::filesystem::weakly_canonical(dir, ec);
        return static_cast<std::size_t>(
            std::ranges::count_if(loader_->modules(), [&root](const atp::runtime::registered_module& m) {
                std::error_code inner;
                return std::filesystem::weakly_canonical(std::filesystem::path(m.source).parent_path(), inner) == root;
            }));
    }

    void create(const std::string& name) {
        module_ = registry_.create(name);
        ASSERT_NE(module_, nullptr);
    }

    void create(const std::string& name, const atp::runtime::config_source& source) {
        module_ = registry_.create(name, filled(name, source));
        ASSERT_NE(module_, nullptr);
    }

    [[nodiscard]] atp::config_ptr filled(const std::string& name, const atp::runtime::config_source& source) {
        atp::config_ptr cfg = registry_.at(name).make_config();
        atp::runtime::load_fields_or_throw(*cfg, source);
        return cfg;
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
    probe_module host_;
    atp::module_ptr module_;
    atp::service_directory services_;
    recording_host log_host_;
    atp::module_context context_{services_, log_host_};
    std::stop_source stop_;
};

std::string unique_module_name(std::string_view stem) {
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::string(stem) + "_" + std::to_string(static_cast<unsigned long long>(ticks));
}

}  // namespace

TEST_F(python_bridge_test, LoadsWithNoModulesWhenNothingIsScanned) {
    scan("");
    load();
    EXPECT_EQ(loader_->modules().size(), registered_from(own_scripts_dir()))
        << "an empty ATP_PYTHON_PATH must add no directory of its own; everything registered has to come "
           "from python/ beside the bridge, which in a build tree and in an installation alike also holds "
           "the sample pipeline's scripts";
}

TEST_F(python_bridge_test, RegistersAModuleDeclaredByAScript) {
    scan(ATP_PYTHON_BRIDGE_SCRIPTS);
    load();
    EXPECT_TRUE(registered("py_declares", atp::version(2, 1)));
    EXPECT_TRUE(registered("py_echo", atp::version(1, 0)));
    EXPECT_EQ(registry_.at("py_declares").get_version(), atp::version(2, 1));
}

TEST_F(python_bridge_test, EachModuleNamesTheScriptItWasReadFrom) {
    scan(ATP_PYTHON_BRIDGE_SCRIPTS);
    load();

    const std::filesystem::path declared_in(source_of("py_declares"));
    EXPECT_EQ(declared_in.filename(), "t_declares.py")
        << "a module name is not a file name, so the script has to be reported rather than guessed";
    EXPECT_TRUE(std::filesystem::exists(declared_in)) << declared_in.string();
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

TEST_F(python_bridge_test, WritesAScriptLogLineThroughTheBoundary) {
    scan(ATP_PYTHON_BRIDGE_SCRIPTS);
    load();
    create("py_echo");
    run_lifecycle();

    const auto written =
        std::ranges::find_if(log_host_.lines, [](const auto& line) { return line.second == "py_echo ready"; });
    ASSERT_NE(written, log_host_.lines.end()) << "log() is the one call every script makes, so the '#' format it "
                                                 "parses with has to be the one Python expects";
    EXPECT_EQ(written->first, atp::log_level::info);
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
    EXPECT_EQ(registered_from(ATP_PYTHON_BRIDGE_SCRIPTS_BROKEN), 1u)
        << "one unimportable script must not take the directory down with it, and the count is of that "
           "directory alone: python/ beside the bridge carries the sample scripts too";
    EXPECT_NE(registry_.find("py_neighbour"), nullptr);
}

TEST_F(python_bridge_test, AModuleWhosePortTypeIsNotAKindIsSkippedRatherThanFatal) {
    scan(ATP_PYTHON_BRIDGE_SCRIPTS_BROKEN);
    load();

    EXPECT_EQ(registry_.find("py_bad_kind"), nullptr);
    EXPECT_NE(registry_.find("py_neighbour"), nullptr)
        << "a descriptor the C ABI cannot express must cost its own module and no more: validate_c_desc "
           "throws from the factory's constructor, so a kind the bridge lets through takes the whole "
           "plugin down and every other script with it";
}

TEST(PythonBridgeReload, AScriptWrittenAfterTheLoadArrivesWithTheNextReload) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "atp_studio_new_python_module";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    atp::studio::apply_script_path({dir.string()}, "", atp::studio::python_language);

    (void)atp::studio::create_script_module(dir, "py_studio_first", atp::studio::python_language);

    atp::studio::module_manager manager;
    manager.load_plugin(ATP_PYTHON_BRIDGE);
    ASSERT_EQ(manager.plugins().size(), 1u);
    ASSERT_TRUE(manager.plugins().front().loaded) << manager.plugins().front().error;
    ASSERT_NE(manager.registry().find("py_studio_first"), nullptr)
        << "scan path: " << atp::studio::inherited_script_path(atp::studio::python_language);

    (void)atp::studio::create_script_module(dir, "py_studio_second", atp::studio::python_language);
    ASSERT_TRUE(manager.reload_plugin(ATP_PYTHON_BRIDGE));

    EXPECT_NE(manager.registry().find("py_studio_first"), nullptr);
    ASSERT_NE(manager.registry().find("py_studio_second"), nullptr);
    EXPECT_EQ(manager.registry().versions("py_studio_first").size(), 1u);
    EXPECT_EQ(manager.registry().versions("py_studio_second").size(), 1u);

    const atp::studio::module_info info =
        atp::studio::module_manager::describe(manager.registry().at("py_studio_second"));
    EXPECT_FALSE(info.broken) << info.error;
    ASSERT_EQ(info.inputs.size(), 1u);
    EXPECT_EQ(info.inputs.front().name, "value");
    ASSERT_EQ(info.outputs.size(), 1u);
    EXPECT_EQ(info.outputs.front().name, "result");
    ASSERT_EQ(info.properties.size(), 1u);
    EXPECT_EQ(info.properties.front().name, "factor");
    EXPECT_EQ(info.properties.front().default_value, "2");

    atp::studio::apply_script_path({}, "", atp::studio::python_language);
    std::filesystem::remove_all(dir);
}

TEST(PythonBridgeReload, ADirectoryThatAppearsAfterTheFirstLoadIsScannedByTheReload) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "atp_studio_late_python_dir";
    std::filesystem::remove_all(dir);
    atp::studio::apply_script_path({}, "", atp::studio::python_language);

    atp::studio::module_manager manager;
    manager.load_plugin(ATP_PYTHON_BRIDGE);
    ASSERT_TRUE(manager.plugins().front().loaded) << manager.plugins().front().error;
    ASSERT_EQ(manager.registry().find("py_studio_late"), nullptr);

    std::filesystem::create_directories(dir);
    (void)atp::studio::create_script_module(dir, "py_studio_late", atp::studio::python_language);
    atp::studio::apply_script_path({dir.string()}, "", atp::studio::python_language);
    ASSERT_TRUE(manager.reload_plugin(ATP_PYTHON_BRIDGE));

    EXPECT_NE(manager.registry().find("py_studio_late"), nullptr)
        << "a script directory named after the bridge was loaded never reached discovery";

    atp::studio::apply_script_path({}, "", atp::studio::python_language);
    std::filesystem::remove_all(dir);
}

TEST(PythonBridgeReload, AProvisionedFolderCarriesABridgeThatFindsTheScriptsBesideIt) {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "atp_studio_provisioned_folder";
    std::filesystem::create_directories(root);
    atp::studio::apply_script_path({}, "", atp::studio::python_language);

    atp::studio::bridge_source source;
    source.bridge = ATP_PYTHON_BRIDGE;
    source.package = std::filesystem::path(ATP_PYTHON_BRIDGE).parent_path() / "python" / "atp";
    ASSERT_TRUE(std::filesystem::exists(source.package)) << source.package.string();

    const atp::studio::folder_setup done = atp::studio::provision_folder(root, source, atp::studio::python_language);
    ASSERT_TRUE(std::filesystem::exists(root / atp::studio::bridge_filename(atp::studio::python_language)));
    ASSERT_TRUE(std::filesystem::exists(done.scripts_dir / "atp" / "__init__.py"));
    const std::string module_name = unique_module_name("py_provisioned");
    (void)atp::studio::create_script_module(done.scripts_dir, module_name, atp::studio::python_language);

    atp::studio::module_manager manager;
    manager.load_plugin(root / atp::studio::bridge_filename(atp::studio::python_language));
    ASSERT_TRUE(manager.plugins().front().loaded) << manager.plugins().front().error;
    EXPECT_NE(manager.registry().find(module_name), nullptr)
        << "the copied bridge did not scan the python/ directory beside itself";

    std::error_code ignored;
    std::filesystem::remove(done.scripts_dir / (module_name + ".py"), ignored);
}

TEST(PythonBridgeReload, ASecondBridgeInOneProcessRegistersButCannotCreate) {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "atp_studio_second_bridge";
    std::filesystem::create_directories(root);
    atp::studio::apply_script_path({}, "", atp::studio::python_language);

    atp::studio::bridge_source source;
    source.bridge = ATP_PYTHON_BRIDGE;
    source.package = std::filesystem::path(ATP_PYTHON_BRIDGE).parent_path() / "python" / "atp";
    const atp::studio::folder_setup done = atp::studio::provision_folder(root, source, atp::studio::python_language);
    const std::string module_name = unique_module_name("py_second_copy");
    (void)atp::studio::create_script_module(done.scripts_dir, module_name, atp::studio::python_language);

    atp::studio::module_manager original;
    original.load_plugin(ATP_PYTHON_BRIDGE);
    ASSERT_TRUE(original.plugins().front().loaded) << original.plugins().front().error;

    atp::studio::module_manager copy;
    copy.load_plugin(root / atp::studio::bridge_filename(atp::studio::python_language));
    ASSERT_TRUE(copy.plugins().front().loaded) << copy.plugins().front().error;
    ASSERT_NE(copy.registry().find(module_name), nullptr);

    const atp::studio::module_info info = atp::studio::module_manager::describe(copy.registry().at(module_name));
    EXPECT_FALSE(info.broken) << "the descriptor is the losing copy's own, so what the module declares is "
                                 "answerable without an interpreter behind it";
    try {
        (void)copy.registry().create(module_name);
        FAIL() << "a second bridge copy sharing one interpreter is expected to fail at creation";
    } catch (const std::exception& e) {
        EXPECT_NE(std::string(e.what()).find("create refused"), std::string::npos) << e.what();
    }

    std::error_code ignored;
    std::filesystem::remove(done.scripts_dir / (module_name + ".py"), ignored);
}

/// What the studio actually does, as opposed to the two-registry case above: one manager, so the
/// second copy tries to register names the first already holds and is withdrawn whole. The row it
/// leaves behind is a red "failed" line about a file the session is deliberately not using, and
/// dropping it is the point of keep_one_python_bridge.
TEST(PythonBridgeReload, ASecondBridgeInOneRegistryIsDroppedRatherThanLeftFailed) {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "atp_studio_second_bridge_one_registry";
    std::filesystem::create_directories(root);
    atp::studio::apply_script_path({}, "", atp::studio::python_language);

    atp::studio::bridge_source source;
    source.bridge = ATP_PYTHON_BRIDGE;
    source.package = std::filesystem::path(ATP_PYTHON_BRIDGE).parent_path() / "python" / "atp";
    const atp::studio::folder_setup done = atp::studio::provision_folder(root, source, atp::studio::python_language);
    const std::string module_name = unique_module_name("py_one_registry");
    (void)atp::studio::create_script_module(done.scripts_dir, module_name, atp::studio::python_language);
    atp::studio::apply_script_path({done.scripts_dir.string()}, "", atp::studio::python_language);

    atp::studio::module_manager manager;
    manager.load_plugin(ATP_PYTHON_BRIDGE);
    ASSERT_TRUE(manager.plugins().front().loaded) << manager.plugins().front().error;
    manager.load_plugin(root / atp::studio::bridge_filename(atp::studio::python_language));
    ASSERT_EQ(manager.plugins().size(), 2u);
    ASSERT_FALSE(manager.plugins().back().loaded) << "one registry cannot hold the same names twice";

    const std::vector<std::filesystem::path> dropped =
        atp::studio::keep_one_bridge(manager, atp::studio::python_language);

    ASSERT_EQ(dropped.size(), 1u);
    EXPECT_EQ(dropped.front().filename(),
              std::filesystem::path(atp::studio::bridge_filename(atp::studio::python_language)));
    ASSERT_EQ(manager.plugins().size(), 1u);
    EXPECT_TRUE(manager.plugins().front().loaded);

    atp::studio::apply_script_path({}, "", atp::studio::python_language);
    std::error_code ignored;
    std::filesystem::remove(done.scripts_dir / (module_name + ".py"), ignored);
}

/// A bridge that failed on its own is the opposite case: with nothing loaded beside it, the error is
/// all the person has, and it is usually about the CPython runtime rather than about the bridge.
TEST(PythonBridgeReload, AFailedBridgeAloneIsKept) {
    atp::studio::module_manager manager;
    manager.load_plugin(std::filesystem::temp_directory_path() /
                        atp::studio::bridge_filename(atp::studio::python_language));
    ASSERT_EQ(manager.plugins().size(), 1u);
    ASSERT_FALSE(manager.plugins().front().loaded);

    EXPECT_TRUE(atp::studio::keep_one_bridge(manager, atp::studio::python_language).empty());
    EXPECT_EQ(manager.plugins().size(), 1u);
}

TEST(PythonBridgeReload, AnEditedScriptReachesThePaletteOnlyThroughReloadAll) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "atp_studio_edited_python_module";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    atp::studio::apply_script_path({dir.string()}, "", atp::studio::python_language);

    const std::string module_name = unique_module_name("py_edited");
    const std::filesystem::path file =
        atp::studio::create_script_module(dir, module_name, atp::studio::python_language);

    atp::studio::module_manager manager;
    manager.load_plugin(ATP_PYTHON_BRIDGE);
    ASSERT_TRUE(manager.plugins().front().loaded) << manager.plugins().front().error;
    ASSERT_NE(manager.registry().find(module_name), nullptr);

    std::ofstream(file, std::ios::trunc) << "import atp\n\n\nclass Edited(atp.Module):\n"
                                         << "    name = \"" << module_name << "\"\n"
                                         << "    version = (2, 4)\n\n"
                                         << "    amount = atp.Input(atp.i32)\n"
                                         << "    result = atp.Output(atp.i32)\n\n"
                                         << "    def iterate(self):\n        return atp.IDLE\n";

    manager.rescan();
    EXPECT_EQ(manager.registry().at(module_name).get_version(), atp::version(1, 0))
        << "a scan is not supposed to re-read a plugin it already loaded";

    manager.reload_all();

    const atp::studio::module_info info = atp::studio::module_manager::describe(manager.registry().at(module_name));
    EXPECT_FALSE(info.broken) << info.error;
    EXPECT_EQ(info.ver, atp::version(2, 4));
    ASSERT_EQ(info.inputs.size(), 1u);
    EXPECT_EQ(info.inputs.front().name, "amount");

    atp::studio::apply_script_path({}, "", atp::studio::python_language);
    std::filesystem::remove_all(dir);
}

/// A module built before a reload keeps a pointer to its descriptor and dereferences it once more
/// when it is destroyed, so re-running discovery must not reuse the array the first batch lives in.
/// The gap this closes is reachable by hand: a stopped pipeline still owns its modules, and the
/// rescan button reloads because nothing is running.
TEST(PythonBridgeReload, AModuleOutlivesTheReloadThatRediscoveredIt) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "atp_studio_reload_outlives";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    atp::studio::apply_script_path({dir.string()}, "", atp::studio::python_language);

    const std::string module_name = unique_module_name("py_outlives");
    (void)atp::studio::create_script_module(dir, module_name, atp::studio::python_language);

    atp::studio::module_manager manager;
    manager.load_plugin(ATP_PYTHON_BRIDGE);
    ASSERT_TRUE(manager.plugins().front().loaded) << manager.plugins().front().error;
    atp::module_ptr built = manager.registry().create(module_name);
    ASSERT_NE(built, nullptr);

    for (int extra = 0; extra < 8; ++extra) {
        (void)atp::studio::create_script_module(dir, module_name + "_x" + std::to_string(extra),
                                                atp::studio::python_language);
    }
    manager.reload_all();

    EXPECT_EQ(built->iterate(std::stop_token{}), atp::work_status::idle);
    built.reset();

    atp::studio::apply_script_path({}, "", atp::studio::python_language);
    std::filesystem::remove_all(dir);
}

TEST(PythonBridgeReload, AScriptDirectoryNamedTwiceIsWalkedOnce) {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "atp_studio_named_twice";
    std::error_code ignored;
    std::filesystem::create_directories(root);

    atp::studio::bridge_source source;
    source.bridge = ATP_PYTHON_BRIDGE;
    source.package = std::filesystem::path(ATP_PYTHON_BRIDGE).parent_path() / "python" / "atp";
    const atp::studio::folder_setup done = atp::studio::provision_folder(root, source, atp::studio::python_language);
    const std::string module_name = unique_module_name("py_named_twice");
    (void)atp::studio::create_script_module(done.scripts_dir, module_name, atp::studio::python_language);
    atp::studio::apply_script_path({done.scripts_dir.string()}, "", atp::studio::python_language);

    atp::studio::module_manager manager;
    manager.load_plugin(root / atp::studio::bridge_filename(atp::studio::python_language));

    EXPECT_TRUE(manager.plugins().front().loaded)
        << "a directory reached both through ATP_PYTHON_PATH and as the bridge's own python/ was walked twice: "
        << manager.plugins().front().error;
    EXPECT_NE(manager.registry().find(module_name), nullptr);

    atp::studio::apply_script_path({}, "", atp::studio::python_language);
    std::filesystem::remove(done.scripts_dir / (module_name + ".py"), ignored);
}

TEST(PythonBridgeReload, ANonAsciiFolderAndScriptNameAreReadAndReported) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / unicode_name("atp_py_", {0x0451, 0x043b, 0x043a, 0x0430}, "");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const std::filesystem::path script =
        root / unicode_name("", {0x0442, 0x005f, 0x043f, 0x0440, 0x043e, 0x0431, 0x0430}, ".py");
    {
        std::ofstream out(script);
        out << R"PY(import atp


class Probe(atp.Module):
    name = "py_non_ascii"
    version = (1, 0)
    value = atp.Input(atp.i32)

    def iterate(self):
        return atp.IDLE
)PY";
    }
#ifdef _WIN32
    _wputenv_s(L"ATP_PYTHON_PATH", root.wstring().c_str());
#else
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    setenv("ATP_PYTHON_PATH", root.string().c_str(), 1);
#endif

    atp::module_registry registry;
    const atp::runtime::module_loader loader(ATP_PYTHON_BRIDGE, registry);

    ASSERT_NE(registry.find("py_non_ascii"), nullptr)
        << "a narrow read of the scan variable replaces what the code page cannot represent, and the "
           "directory is then silently not there";

    std::string reported;
    for (const atp::runtime::registered_module& m : loader.modules()) {
        if (m.name == "py_non_ascii") {
            reported = m.source;
        }
    }
    const std::filesystem::path from_report(
        std::u8string(reinterpret_cast<const char8_t*>(reported.data()), reported.size()));
    EXPECT_EQ(from_report, script) << "the descriptor carries the script as UTF-8";
    EXPECT_TRUE(std::filesystem::exists(from_report));

    atp::studio::apply_script_path({}, "", atp::studio::python_language);
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_F(python_bridge_test, ConfigReachesAScriptAsADict) {
    scan(ATP_PYTHON_BRIDGE_SCRIPTS);
    load();
    create("py_config", {atp::config::node::object({
                             {"channels", atp::config::node::array({1, 2, 6})},
                             {"name", "rig"},
                             {"nested", atp::config::node::object({{"deep", true}})},
                         }),
                         {},
                         {},
                         false});
    collect("out_report", host_.inputs().text);
    run_lifecycle();

    EXPECT_EQ(pass(), atp::work_status::busy);
    EXPECT_EQ(host_.inputs().text.get(), "channels=6 name=rig nested=True keys=channels,name,nested");
}

TEST_F(python_bridge_test, AScriptWithNoConfigSeesNone) {
    scan(ATP_PYTHON_BRIDGE_SCRIPTS);
    load();
    create("py_config");
    collect("out_report", host_.inputs().text);
    run_lifecycle();

    EXPECT_EQ(pass(), atp::work_status::busy);
    EXPECT_EQ(host_.inputs().text.get(), "config=None");
}

TEST_F(python_bridge_test, AnOpaqueConfigReachesAScriptAsTextAndOrigin) {
    scan(ATP_PYTHON_BRIDGE_SCRIPTS);
    load();
    create("py_config_text", {{}, "rate = 48000\n", "rig.ini", true});
    collect("out_report", host_.inputs().text);
    run_lifecycle();

    EXPECT_EQ(pass(), atp::work_status::busy);
    EXPECT_EQ(host_.inputs().text.get(), "text-len=13 origin=rig.ini opaque=True rate=none");
}

TEST_F(python_bridge_test, AParsedConfigLeavesTheTextEmptyAndTheTreeReadable) {
    scan(ATP_PYTHON_BRIDGE_SCRIPTS);
    load();
    create("py_config_text",
           {atp::config::node::object({{"audio", atp::config::node::object({{"rate", 48000}})}}), {}, {}, false});
    collect("out_report", host_.inputs().text);
    run_lifecycle();

    EXPECT_EQ(pass(), atp::work_status::busy);
    EXPECT_EQ(host_.inputs().text.get(), "text-len=0 origin=none opaque=False rate=48000");
}

TEST_F(python_bridge_test, AConfigFileThatIsNotUtf8FailsCreationNamingIt) {
    scan(ATP_PYTHON_BRIDGE_SCRIPTS);
    load();
    try {
        module_ = registry_.create("py_config_text",
                                   filled("py_config_text", {{}, std::string("\xff\xfe rate", 7), "rig.ini", true}));
        FAIL() << "text that is not UTF-8 must not reach a script as if it were";
    } catch (const std::runtime_error& e) {
        const std::string message = e.what();
        EXPECT_NE(message.find("rig.ini"), std::string::npos) << message;
        EXPECT_NE(message.find("UnicodeDecodeError"), std::string::npos) << message;
    }
}

TEST_F(python_bridge_test, ADeclaredConfigReachesAScriptFilledWithItsDefaults) {
    scan(ATP_PYTHON_BRIDGE_SCRIPTS);
    load();
    create("py_declared", {atp::config::node::object({{"rate", 48000}}), {}, {}, false});
    collect("out_report", host_.inputs().text);
    run_lifecycle();

    EXPECT_EQ(pass(), atp::work_status::busy);
    EXPECT_EQ(host_.inputs().text.get(),
              "rate=48000 engine=fm note=60 voices=0 taps=0 keys=rate,engine,master,voices,taps")
        << "only rate was written; every other key is the declaration's own default, in declaration order";
}

TEST_F(python_bridge_test, ADeclaringScriptDescribesItsConfigWithoutBeingBuilt) {
    scan(ATP_PYTHON_BRIDGE_SCRIPTS);
    load();
    const atp::config_ptr cfg = registry_.at("py_declared").make_config();

    ASSERT_NE(cfg, nullptr);
    ASSERT_EQ(cfg->entries().size(), 5U);
    EXPECT_TRUE(cfg->find("rate")->required());
    EXPECT_EQ(cfg->find("engine")->options(), (std::vector<std::string>{"fm", "additive"}));
    EXPECT_EQ(cfg->find("master")->group().find("note")->value<std::int64_t>(), 60);
    EXPECT_EQ(cfg->find("voices")->element_shape().entries()[0].name(), "note");
    EXPECT_EQ(cfg->find("taps")->element(), atp::field_kind::real);
}

TEST_F(python_bridge_test, ARequiredFieldAScriptDeclaredIsCheckedAgainstTheDocument) {
    scan(ATP_PYTHON_BRIDGE_SCRIPTS);
    load();
    const atp::config_ptr cfg = registry_.at("py_declared").make_config();
    const std::vector<std::string> problems =
        atp::runtime::load_fields(*cfg, {atp::config::node::object({}), {}, {}, false});

    ASSERT_EQ(problems.size(), 1U);
    EXPECT_NE(problems[0].find("rate"), std::string::npos);
}
