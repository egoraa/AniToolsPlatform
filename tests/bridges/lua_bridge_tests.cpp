// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stop_token>
#include <string>
#include <system_error>
#include <typeindex>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <atp/hosting/module_registry.hpp>
#include <atp/hosting/null_host.hpp>
#include <atp/io.hpp>
#include <atp/module.hpp>
#include <atp/module/module_base.hpp>
#include <atp/module/module_context.hpp>
#include <atp/module/service_directory.hpp>
#include <atp/runtime/config_binding.hpp>
#include <atp/runtime/module_loader.hpp>
#include <atp/studio/languages.hpp>
#include <atp/studio/module_manager.hpp>
#include <atp/studio/script_modules.hpp>
#include <atp/support/version.hpp>

namespace {

constexpr char scan_separator =
#ifdef _WIN32
    ';';
#else
    ':';
#endif

void set_scan_path(const std::string& value) {
#ifdef _WIN32
    _putenv_s("ATP_LUA_PATH", value.c_str());
#else
    setenv("ATP_LUA_PATH", value.c_str(), 1);
#endif
}

void set_scan_path_wide(const std::filesystem::path& dir) {
#ifdef _WIN32
    _wputenv_s(L"ATP_LUA_PATH", dir.wstring().c_str());
#else
    setenv("ATP_LUA_PATH", dir.string().c_str(), 1);
#endif
}

void write_module(const std::filesystem::path& file, const std::string& name) {
    std::ofstream out(file);
    out << "local atp = require(\"atp\")\n"
        << "local M = atp.module(\"" << name << "\", { 1, 0 })\n"
        << "M.value = atp.input(atp.i32)\n"
        << "M.result = atp.output(atp.i32)\n"
        << "M.factor = atp.property(atp.i32, 2)\n"
        << "function M:iterate() return atp.IDLE end\n"
        << "return M\n";
}

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
    return std::filesystem::path(name);
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

class probe_module : public atp::module<atp::ports<probe_inputs, probe_outputs>, "lua_test_host"> {};

class lua_bridge_test : public ::testing::Test {
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

    static void scan(const char* dir) {
        set_scan_path(dir);
    }

    void load() {
        loader_.emplace(ATP_LUA_BRIDGE, registry_);
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
        return std::filesystem::path(ATP_LUA_BRIDGE).parent_path() / "lua";
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
    atp::null_host log_host_;
    atp::module_context context_{services_, log_host_};
    std::stop_source stop_;
};

}  // namespace

TEST_F(lua_bridge_test, LoadsWithNoModulesWhenNothingIsScanned) {
    scan("");
    load();
    EXPECT_EQ(loader_->modules().size(), registered_from(own_scripts_dir()))
        << "an empty ATP_LUA_PATH must add no directory of its own; everything registered has to come from "
           "lua/ beside the bridge, which in a build tree and in an installation alike also holds the "
           "sample pipeline's scripts";
}

TEST_F(lua_bridge_test, RegistersAModuleDeclaredByAScript) {
    scan(ATP_LUA_BRIDGE_SCRIPTS);
    load();
    EXPECT_TRUE(registered("lua_declares", atp::version(2, 1)));
    EXPECT_TRUE(registered("lua_echo", atp::version(1, 0)));
    EXPECT_EQ(registry_.at("lua_declares").get_version(), atp::version(2, 1));
}

TEST_F(lua_bridge_test, EachModuleNamesTheScriptItWasReadFrom) {
    scan(ATP_LUA_BRIDGE_SCRIPTS);
    load();

    const std::filesystem::path declared_in(source_of("lua_declares"));
    EXPECT_EQ(declared_in.filename(), "t_declares.lua")
        << "a module name is not a file name, so the script has to be reported rather than guessed";
    EXPECT_TRUE(std::filesystem::exists(declared_in)) << declared_in.string();
}

TEST_F(lua_bridge_test, BuildsPlatformPortsFromTheDeclarations) {
    scan(ATP_LUA_BRIDGE_SCRIPTS);
    load();
    create("lua_declares");

    EXPECT_EQ(module_->inputs().list().size(), 3u);
    EXPECT_EQ(module_->outputs().list().size(), 2u);
    EXPECT_TRUE(module_->inputs().at("value").accepts(std::type_index(typeid(std::int32_t))));
    EXPECT_TRUE(module_->inputs().at("queued").accepts(std::type_index(typeid(std::string))));
    EXPECT_TRUE(module_->inputs().at("flag").accepts(std::type_index(typeid(bool))));
    EXPECT_NO_THROW(collect("result", host_.inputs().i32))
        << "an output carries no type query, so the declared kind is checked the way the platform does it";
}

TEST_F(lua_bridge_test, KeepsTheOrderTheScriptDeclaredPortsIn) {
    scan(ATP_LUA_BRIDGE_SCRIPTS);
    load();

    const std::vector<std::pair<std::string, std::int32_t>> expected{{"first", 1}, {"second", 2}, {"third", 4}};
    atp::io::output_base& source = host_.outputs().i32;
    for (const auto& [port, mark] : expected) {
        atp::module_ptr instance = registry_.create("lua_order");
        ASSERT_NE(instance, nullptr);
        source.connect(instance->inputs().at(port));
        instance->outputs().at("mark").connect(host_.inputs().i32);

        host_.outputs().i32(9);
        instance->initialize(context_);
        instance->start();
        EXPECT_EQ(instance->iterate(stop_.get_token()), atp::work_status::busy);
        EXPECT_EQ(host_.inputs().i32.get(), mark)
            << "the host connected to '" << port
            << "' by name and the script reads by index, so the declaration order has to be the same on both sides";
        instance->stop();

        source.disconnect_all();
        instance->outputs().at("mark").disconnect_all();
    }
}

TEST_F(lua_bridge_test, BuildsPropertiesWithDefaultsAndOptions) {
    scan(ATP_LUA_BRIDGE_SCRIPTS);
    load();
    create("lua_declares");

    EXPECT_EQ(module_->properties().at("factor").to_string(), "2");
    EXPECT_EQ(module_->properties().at("channels").options().size(), 3u);
    EXPECT_TRUE(module_->properties().at("factor").persistent());
    EXPECT_FALSE(module_->properties().at("transient").persistent());
}

TEST_F(lua_bridge_test, CarriesEveryKindInBothDirections) {
    scan(ATP_LUA_BRIDGE_SCRIPTS);
    load();
    create("lua_echo");
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
    EXPECT_EQ(host_.inputs().flag.get(), false);
    EXPECT_EQ(host_.inputs().text.get(), "hi!");
    EXPECT_EQ(host_.inputs().bytes.get(), (atp::io::blob{std::byte{2}, std::byte{1}}));
}

TEST_F(lua_bridge_test, ReadsAPropertyThroughTheBoundary) {
    scan(ATP_LUA_BRIDGE_SCRIPTS);
    load();
    create("lua_echo");
    feed(host_.outputs().i32, "in_i32");
    collect("out_gain", host_.inputs().f64);
    module_->properties().at("gain").from_string("2.5");

    host_.outputs().i32(2);
    run_lifecycle();
    EXPECT_EQ(pass(), atp::work_status::busy);
    EXPECT_EQ(host_.inputs().f64.get(), 5.0);
}

TEST_F(lua_bridge_test, RefusesAValueTooLargeForItsPort) {
    scan(ATP_LUA_BRIDGE_SCRIPTS);
    load();
    create("lua_echo");
    feed(host_.outputs().i32, "in_i32");
    host_.outputs().i32(1);
    run_lifecycle();
    module_->properties().at("overflow").from_string("true");

    try {
        (void)pass();
        FAIL() << "writing 2^31 to an i32 port must not be silently truncated";
    } catch (const std::exception& error) {
        const std::string text = error.what();
        EXPECT_NE(text.find("lua_echo"), std::string::npos) << text;
        EXPECT_NE(text.find("does not fit an i32 port"), std::string::npos) << text;
    }
}

TEST_F(lua_bridge_test, ReportsAScriptErrorAsAModuleError) {
    scan(ATP_LUA_BRIDGE_SCRIPTS);
    load();
    create("lua_raises");
    run_lifecycle();

    try {
        (void)pass();
        FAIL() << "an error inside iterate has to reach the host";
    } catch (const std::exception& error) {
        const std::string text = error.what();
        EXPECT_NE(text.find("t_raises.lua"), std::string::npos) << text;
        EXPECT_NE(text.find("iterate"), std::string::npos) << text;
        EXPECT_NE(text.find("stack traceback"), std::string::npos)
            << "the handler is there so the path that reached the error is readable: " << text;
    }
}

TEST_F(lua_bridge_test, SkipsABrokenScriptAndKeepsItsNeighbour) {
    scan(ATP_LUA_BRIDGE_SCRIPTS_BROKEN);
    load();
    EXPECT_EQ(registered_from(ATP_LUA_BRIDGE_SCRIPTS_BROKEN), 1u)
        << "one unreadable script must not take the directory down with it, and the count is of that "
           "directory alone: lua/ beside the bridge carries the sample scripts too";
    EXPECT_NE(registry_.find("lua_neighbour"), nullptr);
}

TEST_F(lua_bridge_test, AScriptThatReplacesTheDiscoveryHookIsSkippedRatherThanFatal) {
    scan(ATP_LUA_BRIDGE_SCRIPTS_BROKEN);
    load();

    EXPECT_NE(registry_.find("lua_neighbour"), nullptr)
        << "reaching this line at all is the assertion: without the type check the rows a hijacked "
           "_declared returns are read outside any protected frame, and Lua panics the whole process";
}

TEST_F(lua_bridge_test, AModuleWhosePortTypeIsNotAKindIsSkippedRatherThanFatal) {
    scan(ATP_LUA_BRIDGE_SCRIPTS_BROKEN);
    load();

    EXPECT_EQ(registry_.find("lua_bad_kind"), nullptr);
    EXPECT_NE(registry_.find("lua_neighbour"), nullptr)
        << "a descriptor the C ABI cannot express must cost its own module and no more: validate_c_desc "
           "throws from the factory's constructor, so a kind the bridge lets through takes the whole "
           "plugin down and every other script with it";
}

TEST(LuaBridgeReload, AScriptWrittenAfterTheLoadArrivesWithTheNextReload) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "atp_lua_reload";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    set_scan_path(dir.string());
    write_module(dir / "first.lua", "lua_reload_first");

    atp::studio::module_manager manager;
    manager.load_plugin(ATP_LUA_BRIDGE);
    ASSERT_EQ(manager.plugins().size(), 1u);
    ASSERT_TRUE(manager.plugins().front().loaded) << manager.plugins().front().error;
    ASSERT_NE(manager.registry().find("lua_reload_first"), nullptr);

    write_module(dir / "second.lua", "lua_reload_second");
    ASSERT_TRUE(manager.reload_plugin(ATP_LUA_BRIDGE));

    EXPECT_NE(manager.registry().find("lua_reload_first"), nullptr);
    ASSERT_NE(manager.registry().find("lua_reload_second"), nullptr);

    const atp::studio::module_info info =
        atp::studio::module_manager::describe(manager.registry().at("lua_reload_second"));
    EXPECT_FALSE(info.broken) << info.error;
    ASSERT_EQ(info.inputs.size(), 1u);
    EXPECT_EQ(info.inputs.front().name, "value");
    ASSERT_EQ(info.properties.size(), 1u);
    EXPECT_EQ(info.properties.front().default_value, "2");

    set_scan_path("");
    std::error_code ignored;
    std::filesystem::remove_all(dir, ignored);
}

TEST(LuaBridgeReload, AScriptDirectoryNamedTwiceIsWalkedOnce) {
    const std::string twice =
        std::string(ATP_LUA_BRIDGE_SCRIPTS) + scan_separator + std::string(ATP_LUA_BRIDGE_SCRIPTS);
    set_scan_path(twice);

    atp::module_registry registry;
    EXPECT_NO_THROW({
        const atp::runtime::module_loader loader(ATP_LUA_BRIDGE, registry);
        EXPECT_NE(registry.find("lua_declares"), nullptr);
    }) << "walking a directory twice registers every name twice and the whole plugin is refused";
    set_scan_path("");
}

TEST(LuaBridgeReload, ASecondBridgeInOneRegistryIsDroppedRatherThanLeftFailed) {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "atp_lua_second_bridge";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    set_scan_path("");

    atp::studio::bridge_source source;
    source.bridge = ATP_LUA_BRIDGE;
    source.package = std::filesystem::path(ATP_LUA_BRIDGE).parent_path() / "lua" / "atp.lua";
    const atp::studio::folder_setup done = atp::studio::provision_folder(root, source, atp::studio::lua_language);
    write_module(done.scripts_dir / "one_registry.lua", "lua_one_registry");
    set_scan_path(done.scripts_dir.string());

    atp::studio::module_manager manager;
    manager.load_plugin(ATP_LUA_BRIDGE);
    ASSERT_TRUE(manager.plugins().front().loaded) << manager.plugins().front().error;
    manager.load_plugin(root / atp::studio::bridge_filename(atp::studio::lua_language));
    ASSERT_EQ(manager.plugins().size(), 2u);
    ASSERT_FALSE(manager.plugins().back().loaded)
        << "one registry cannot hold the same names twice, whatever language the bridge speaks";

    const std::vector<std::filesystem::path> dropped = atp::studio::keep_one_bridge(manager, atp::studio::lua_language);

    ASSERT_EQ(dropped.size(), 1u) << "an interpreter per instance does not make a second copy harmless";
    ASSERT_EQ(manager.plugins().size(), 1u);
    EXPECT_TRUE(manager.plugins().front().loaded);

    set_scan_path("");
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST(LuaBridgeReload, ANonAsciiFolderAndScriptNameAreReadAndReported) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / unicode_name("atp_lua_", {0x0451, 0x043b, 0x043a, 0x0430}, "");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const std::filesystem::path script =
        root / unicode_name("", {0x0442, 0x005f, 0x043f, 0x0440, 0x043e, 0x0431, 0x0430}, ".lua");
    write_module(script, "lua_non_ascii");
    set_scan_path_wide(root);

    atp::module_registry registry;
    const atp::runtime::module_loader loader(ATP_LUA_BRIDGE, registry);

    ASSERT_NE(registry.find("lua_non_ascii"), nullptr)
        << "a narrow path converts through the process code page: the directory used to be silently "
           "absent, and the script name used to throw out of an extern \"C\" entry point";

    std::string reported;
    for (const atp::runtime::registered_module& m : loader.modules()) {
        if (m.name == "lua_non_ascii") {
            reported = m.source;
        }
    }
    const std::filesystem::path from_report(
        std::u8string(reinterpret_cast<const char8_t*>(reported.data()), reported.size()));
    EXPECT_EQ(from_report, script) << "the descriptor carries the script as UTF-8";
    EXPECT_TRUE(std::filesystem::exists(from_report));

    const atp::module_ptr module = registry.create("lua_non_ascii");
    EXPECT_NE(module, nullptr) << "creating re-executes the file, so it has to open by the same path";

    set_scan_path("");
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_F(lua_bridge_test, ConfigReachesAScriptAsATable) {
    scan(ATP_LUA_BRIDGE_SCRIPTS);
    load();
    create("lua_config", {atp::config::node::object({
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
    EXPECT_EQ(host_.inputs().text.get(), "channels=6 name=rig nested=true keys=channels,name,nested");
}

TEST_F(lua_bridge_test, AScriptWithNoConfigSeesNil) {
    scan(ATP_LUA_BRIDGE_SCRIPTS);
    load();
    create("lua_config");
    collect("out_report", host_.inputs().text);
    run_lifecycle();

    EXPECT_EQ(pass(), atp::work_status::busy);
    EXPECT_EQ(host_.inputs().text.get(), "config=nil");
}

TEST_F(lua_bridge_test, AnOpaqueConfigReachesAScriptAsTextAndOrigin) {
    scan(ATP_LUA_BRIDGE_SCRIPTS);
    load();
    create("lua_config_text", {{}, "rate = 48000\n", "rig.ini", true});
    collect("out_report", host_.inputs().text);
    run_lifecycle();

    EXPECT_EQ(pass(), atp::work_status::busy);
    EXPECT_EQ(host_.inputs().text.get(), "text-len=13 origin=rig.ini opaque=true rate=none");
}

TEST_F(lua_bridge_test, AParsedConfigLeavesTheTextEmptyAndTheTableReadable) {
    scan(ATP_LUA_BRIDGE_SCRIPTS);
    load();
    create("lua_config_text",
           {atp::config::node::object({{"audio", atp::config::node::object({{"rate", 48000}})}}), {}, {}, false});
    collect("out_report", host_.inputs().text);
    run_lifecycle();

    EXPECT_EQ(pass(), atp::work_status::busy);
    EXPECT_EQ(host_.inputs().text.get(), "text-len=0 origin=none opaque=false rate=48000");
}

TEST_F(lua_bridge_test, TextThatIsNotUtf8ReachesAScriptAsBytes) {
    scan(ATP_LUA_BRIDGE_SCRIPTS);
    load();
    create("lua_config_text", {{}, std::string("\xff\xfe rate", 7), "rig.ini", true});
    collect("out_report", host_.inputs().text);
    run_lifecycle();

    EXPECT_EQ(pass(), atp::work_status::busy);
    EXPECT_EQ(host_.inputs().text.get(), "text-len=7 origin=rig.ini opaque=true rate=none");
}

TEST_F(lua_bridge_test, ADeclaredConfigReachesAScriptFilledWithItsDefaults) {
    scan(ATP_LUA_BRIDGE_SCRIPTS);
    load();
    create("lua_declared", {atp::config::node::object({{"rate", 48000}}), {}, {}, false});
    collect("out_report", host_.inputs().text);
    run_lifecycle();

    EXPECT_EQ(pass(), atp::work_status::busy);
    EXPECT_EQ(host_.inputs().text.get(), "rate=48000 engine=fm note=60 voices=0 taps=0")
        << "only rate was written; every other key is the declaration's own default";
}

TEST_F(lua_bridge_test, ADeclaringScriptDescribesItsConfigWithoutBeingBuilt) {
    scan(ATP_LUA_BRIDGE_SCRIPTS);
    load();
    const atp::config_ptr cfg = registry_.at("lua_declared").make_config();

    ASSERT_NE(cfg, nullptr);
    ASSERT_EQ(cfg->entries().size(), 5U);
    EXPECT_EQ(cfg->entries()[0].name(), "rate") << "a Lua table has no order, so the proxy is what keeps it";
    EXPECT_EQ(cfg->entries()[1].name(), "engine");
    EXPECT_EQ(cfg->entries()[2].name(), "master");
    EXPECT_EQ(cfg->entries()[3].name(), "voices");
    EXPECT_EQ(cfg->entries()[4].name(), "taps");
    EXPECT_TRUE(cfg->find("rate")->required());
    EXPECT_EQ(cfg->find("engine")->options(), (std::vector<std::string>{"fm", "additive"}));
    EXPECT_EQ(cfg->find("voices")->element_shape().entries()[0].name(), "note");
}
