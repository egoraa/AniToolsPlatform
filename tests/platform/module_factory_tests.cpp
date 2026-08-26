// SPDX-License-Identifier: Apache-2.0
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include <atp/hosting/module_factory.hpp>
#include <atp/hosting/module_registry.hpp>
#include <atp/module.hpp>

namespace {

class plain_module : public atp::module<> {};

struct channels_config : atp::module_config {
    using module_config::module_config;
    std::int64_t& channels = field("channels", std::int64_t{0});
};

class config_reading_module : public atp::module<atp::ports<>, "config_reader", atp::version{1, 0}> {
   public:
    using config_type = channels_config;

    explicit config_reading_module(std::unique_ptr<channels_config> cfg) : config_(std::move(cfg)) {}

    [[nodiscard]] std::int64_t channels() const {
        return config_->channels;
    }

   private:
    std::unique_ptr<channels_config> config_;
};

struct audio_config : atp::module_config {
    using module_config::module_config;
    std::int64_t& rate = field("rate", std::int64_t{8000});
};

struct deep_config : atp::module_config {
    using module_config::module_config;
    audio_config& audio = group<audio_config>("audio");
};

class deep_config_module : public atp::module<atp::ports<>, "deep_config"> {
   public:
    using config_type = deep_config;

    explicit deep_config_module(std::unique_ptr<deep_config> cfg) : config_(std::move(cfg)) {}

    [[nodiscard]] std::int64_t rate() const {
        return config_->audio.rate;
    }

    [[nodiscard]] const std::string& origin() const {
        return config_->origin();
    }

   private:
    std::unique_ptr<deep_config> config_;
};

class versioned_module : public atp::module<atp::ports<>, "", atp::version{2, 1}> {};

class bare_module : public atp::module_base {
   public:
    void initialize(atp::module_context&) override {}
    void start() override {}
    atp::work_status iterate(std::stop_token) override {
        return atp::work_status::idle;
    }
    void stop() override {}

    atp::io::inputs& inputs() override {
        return inputs_;
    }
    const atp::io::inputs& inputs() const override {
        return inputs_;
    }
    atp::io::outputs& outputs() override {
        return outputs_;
    }
    const atp::io::outputs& outputs() const override {
        return outputs_;
    }
    atp::io::properties& properties() override {
        return properties_;
    }
    const atp::io::properties& properties() const override {
        return properties_;
    }

   private:
    atp::io::inputs inputs_;
    atp::io::outputs outputs_;
    atp::io::properties properties_;
};

class configured_module : public atp::module<> {
   public:
    explicit configured_module(int value) : value_(value) {}

    [[nodiscard]] int value() const {
        return value_;
    }

   private:
    int value_;
};

[[nodiscard]] atp::config_ptr filled(const atp::module_factory_base& factory, std::int64_t channels) {
    atp::config_ptr cfg = factory.make_config();
    cfg->find("channels")->set(channels);
    return cfg;
}

}  // namespace

TEST(ModuleFactory, NameIsStoredFromRegistration) {
    atp::module_factory<plain_module> factory{"plain"};
    EXPECT_EQ(factory.name(), "plain");
}

TEST(ModuleFactory, VersionWithoutInstantiation) {
    atp::module_factory<versioned_module> factory{"versioned"};
    EXPECT_EQ(factory.get_version(), atp::version(2, 1));
}

TEST(ModuleFactory, VersionFallsBackToDefault) {
    atp::module_factory<bare_module> factory{"bare"};
    EXPECT_EQ(factory.get_version(), atp::default_version);
}

TEST(ModuleFactory, CreateReturnsWorkingModule) {
    atp::module_factory<versioned_module> factory{"versioned"};
    atp::module_ptr module = factory.create(factory.make_config());
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->get_version(), atp::version(2, 1));
    EXPECT_NE(factory.create(factory.make_config()), module);
}

TEST(ModuleFactory, TypeErasedThroughBase) {
    atp::module_factory<plain_module> typed{"plain"};
    atp::module_factory_base& factory = typed;
    EXPECT_EQ(factory.name(), "plain");
    EXPECT_NE(factory.create(factory.make_config()), nullptr);
}

TEST(ModuleFactory, CreateReturnsModulePtrWithEmptyPin) {
    atp::module_factory<bare_module> factory{"bare"};
    atp::module_ptr module = factory.create(factory.make_config());
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module.get_deleter().pin, nullptr);
}

TEST(ModuleFactory, CreateBindsConstructorArgs) {
    atp::module_factory<configured_module, int> factory{"cfg", 42};
    atp::module_ptr first = factory.create(factory.make_config());
    atp::module_ptr second = factory.create(factory.make_config());
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_NE(first, second);
    EXPECT_EQ(dynamic_cast<configured_module&>(*first).value(), 42);
    EXPECT_EQ(dynamic_cast<configured_module&>(*second).value(), 42);
}

TEST(ModuleFactory, BoundArgsReachEveryInstance) {
    atp::module_factory<configured_module, int> factory("configured", 42);
    EXPECT_EQ(dynamic_cast<configured_module&>(*factory.create(factory.make_config())).value(), 42);
}

TEST(ModuleFactory, ConfigReachesTheConstructor) {
    const atp::module_factory<config_reading_module> factory("config_reader");
    const atp::module_ptr m = factory.create(filled(factory, 6));
    EXPECT_EQ(dynamic_cast<config_reading_module&>(*m).channels(), 6);
}

TEST(ModuleFactory, ModuleThatIgnoresConfigIsBuiltUnchanged) {
    const atp::module_factory<configured_module, int> factory("configured", 42);
    EXPECT_EQ(dynamic_cast<configured_module&>(*factory.create(factory.make_config())).value(), 42);
}

TEST(ModuleRegistry, AModuleWhoseOnlyConstructorTakesAConfigIsRegisteredLikeAnyOther) {
    atp::module_registry registry;
    registry.add<config_reading_module>();

    const atp::module_ptr m = registry.create("config_reader", filled(registry.at("config_reader"), 6));
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(dynamic_cast<config_reading_module&>(*m).channels(), 6);
}

TEST(ModuleFactory, ConstructorReadsANestedGroupAndTheOrigin) {
    const atp::module_factory<deep_config_module> factory("deep_config");
    atp::config_ptr cfg = factory.make_config();
    cfg->find("audio")->group().find("rate")->set(std::int64_t{48000});
    cfg->attach_source("{}", "rig.json", false);

    const atp::module_ptr m = factory.create(std::move(cfg));
    EXPECT_EQ(dynamic_cast<deep_config_module&>(*m).rate(), 48000);
    EXPECT_EQ(dynamic_cast<deep_config_module&>(*m).origin(), "rig.json");
}

TEST(ModuleRegistry, CreateWithoutAConfigStillReachesAConstructorThatWantsOne) {
    atp::module_registry registry;
    registry.add<deep_config_module>();

    const atp::module_ptr m = registry.create("deep_config");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(dynamic_cast<deep_config_module&>(*m).rate(), 8000);
    EXPECT_TRUE(dynamic_cast<deep_config_module&>(*m).origin().empty());
}

namespace {

struct static_in : atp::io::inputs {
    atp::io::input<int>& value = make<int>("value");
};
using static_ports = atp::ports<static_in>;

class never_constructed : public atp::module<static_ports, "never_constructed"> {
   public:
    never_constructed() {
        throw std::runtime_error("this constructor must not run");
    }
};

class handmade : public atp::module_base {
   public:
    handmade() {
        in_.make<atp::io::input<int>>("handmade_in");
    }
    void initialize(atp::module_context&) override {}
    void start() override {}
    atp::work_status iterate(std::stop_token) override {
        return atp::work_status::idle;
    }
    void stop() override {}
    [[nodiscard]] atp::io::inputs& inputs() override {
        return in_;
    }
    [[nodiscard]] const atp::io::inputs& inputs() const override {
        return in_;
    }
    [[nodiscard]] atp::io::outputs& outputs() override {
        return out_;
    }
    [[nodiscard]] const atp::io::outputs& outputs() const override {
        return out_;
    }
    [[nodiscard]] atp::io::properties& properties() override {
        return props_;
    }
    [[nodiscard]] const atp::io::properties& properties() const override {
        return props_;
    }

   private:
    atp::io::inputs in_;
    atp::io::outputs out_;
    atp::io::properties props_;
};

class handmade_throwing : public handmade {
   public:
    handmade_throwing() {
        throw std::runtime_error("handmade constructor failed");
    }
};

}  // namespace

TEST(ModuleFactory, DeclarationDoesNotRunTheConstructor) {
    const atp::module_factory<never_constructed> factory("never_constructed");
    const atp::module_declaration decl = factory.declaration();

    ASSERT_EQ(decl.inputs.size(), 1u);
    EXPECT_EQ(decl.inputs[0].name, "value");
    EXPECT_THROW((void)factory.create(factory.make_config()), std::runtime_error);
}

TEST(ModuleFactory, AModuleWithoutAPortNodeIsStillDescribedByProbing) {
    const atp::module_factory<handmade> factory("handmade");
    const atp::module_declaration decl = factory.declaration();

    ASSERT_EQ(decl.inputs.size(), 1u);
    EXPECT_EQ(decl.inputs[0].name, "handmade_in");
}

TEST(ModuleFactory, AProbedModuleWithAThrowingConstructorStillThrows) {
    const atp::module_factory<handmade_throwing> factory("handmade_throwing");
    EXPECT_THROW((void)factory.declaration(), std::runtime_error);
}

namespace {

struct sized_config : atp::module_config {
    using module_config::module_config;
    std::int64_t& size = field("size", std::int64_t{16});
    std::string& device = field<std::string>("device");
};

class declared_config_module : public atp::module<atp::ports<>, "configured"> {
   public:
    using config_type = sized_config;

    explicit declared_config_module(std::unique_ptr<sized_config> cfg) : config_(std::move(cfg)) {}

    [[nodiscard]] std::int64_t size() const {
        return config_->size;
    }

   private:
    std::unique_ptr<sized_config> config_;
};

class settingless_module : public atp::module<atp::ports<>, "settingless"> {
   public:
    using config_type = atp::module_config;

    explicit settingless_module(std::unique_ptr<atp::module_config>) {}
};

}  // namespace

TEST(ModuleFactory, TheMadeConfigCarriesEveryDeclaredField) {
    const atp::module_factory<declared_config_module> factory("configured");
    const atp::config_ptr made = factory.make_config();

    ASSERT_NE(made, nullptr);
    const std::span<const atp::module_config::entry> fields = made->entries();
    ASSERT_EQ(fields.size(), 2u);
    EXPECT_EQ(fields[0].name(), "size");
    EXPECT_EQ(fields[0].kind(), atp::field_kind::integer);
    EXPECT_FALSE(fields[0].required());
    EXPECT_EQ(fields[1].name(), "device");
    EXPECT_TRUE(fields[1].required());
}

TEST(ModuleFactory, NoConfigAndAConfigWithNoFieldsAreDifferentAnswers) {
    EXPECT_EQ(atp::module_factory<never_constructed>("never_constructed").make_config(), nullptr)
        << "a module that declares none says so by making none, which is how a host knows to edit it as text";

    const atp::config_ptr empty = atp::module_factory<settingless_module>("settingless").make_config();
    ASSERT_NE(empty, nullptr);
    EXPECT_TRUE(empty->entries().empty()) << "and this one takes a config that has no settings at all";
}

TEST(ModuleFactory, AConfigOfAnotherModuleIsRefused) {
    atp::module_registry registry;
    registry.add<declared_config_module>("with_config");
    registry.add<config_reading_module>("other");

    EXPECT_THROW((void)registry.at("with_config").create(registry.at("other").make_config()),
                 atp::config::access_error);
}

TEST(ModuleFactory, CreateAcceptsTheConfigItMade) {
    const atp::module_factory<declared_config_module> factory("configured");
    atp::config_ptr cfg = factory.make_config();
    cfg->find("size")->set(std::int64_t{32});
    cfg->find("device")->set(std::string("hw:0"));

    const atp::module_ptr m = factory.create(std::move(cfg));
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(dynamic_cast<declared_config_module&>(*m).size(), 32);
}
