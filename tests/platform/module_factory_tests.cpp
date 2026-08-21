// SPDX-License-Identifier: Apache-2.0
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include <atp/config/read.hpp>
#include <atp/hosting/module_factory.hpp>
#include <atp/hosting/module_registry.hpp>
#include <atp/module.hpp>

namespace {

class plain_module : public atp::module<> {};

class config_reading_module : public atp::module<atp::ports<>, "config_reader", atp::version{1, 0}> {
   public:
    explicit config_reading_module(const atp::module_config& cfg)
        : channels_(atp::config::int_or(cfg.find("channels"), 0)) {}

    [[nodiscard]] std::int64_t channels() const {
        return channels_;
    }

   private:
    std::int64_t channels_;
};

class deep_config_module : public atp::module<atp::ports<>, "deep_config"> {
   public:
    explicit deep_config_module(const atp::module_config& cfg)
        : rate_(atp::config::int_or(cfg.find("audio.rate"), 8000)), origin_(cfg.origin()) {}

    [[nodiscard]] std::int64_t rate() const {
        return rate_;
    }

    [[nodiscard]] const std::string& origin() const {
        return origin_;
    }

   private:
    std::int64_t rate_;
    std::string origin_;
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
    atp::module_ptr module = factory.create(atp::module_config{});
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->get_version(), atp::version(2, 1));
    EXPECT_NE(factory.create(atp::module_config{}), module);
}

TEST(ModuleFactory, TypeErasedThroughBase) {
    atp::module_factory<plain_module> typed{"plain"};
    atp::module_factory_base& factory = typed;
    EXPECT_EQ(factory.name(), "plain");
    EXPECT_NE(factory.create(atp::module_config{}), nullptr);
}

TEST(ModuleFactory, CreateReturnsModulePtrWithEmptyPin) {
    atp::module_factory<bare_module> factory{"bare"};
    atp::module_ptr module = factory.create(atp::module_config{});
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module.get_deleter().pin, nullptr);
}

TEST(ModuleFactory, CreateBindsConstructorArgs) {
    atp::module_factory<configured_module, int> factory{"cfg", 42};
    atp::module_ptr first = factory.create(atp::module_config{});
    atp::module_ptr second = factory.create(atp::module_config{});
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_NE(first, second);
    EXPECT_EQ(dynamic_cast<configured_module&>(*first).value(), 42);
    EXPECT_EQ(dynamic_cast<configured_module&>(*second).value(), 42);
}

TEST(ModuleFactory, BoundArgsReachEveryInstance) {
    atp::module_factory<configured_module, int> factory("configured", 42);
    EXPECT_EQ(dynamic_cast<configured_module&>(*factory.create(atp::module_config{})).value(), 42);
}

TEST(ModuleFactory, ConfigReachesTheConstructor) {
    const atp::module_config cfg(atp::config::node::object({{"channels", 6}}));
    const atp::module_factory<config_reading_module> factory("config_reader");
    const atp::module_ptr m = factory.create(cfg);
    EXPECT_EQ(dynamic_cast<config_reading_module&>(*m).channels(), 6);
}

TEST(ModuleFactory, ModuleThatIgnoresConfigIsBuiltUnchanged) {
    const atp::module_factory<configured_module, int> factory("configured", 42);
    EXPECT_EQ(dynamic_cast<configured_module&>(*factory.create(atp::module_config{})).value(), 42);
}

TEST(ModuleRegistry, AModuleWhoseOnlyConstructorTakesAConfigIsRegisteredLikeAnyOther) {
    atp::module_registry registry;
    registry.add<config_reading_module>();

    const atp::module_ptr m =
        registry.create("config_reader", atp::module_config(atp::config::node::object({{"channels", 6}})));
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(dynamic_cast<config_reading_module&>(*m).channels(), 6);
}

TEST(ModuleFactory, ConstructorReadsANestedPathAndTheOrigin) {
    const atp::module_config cfg(atp::config::node::object({{"audio", atp::config::node::object({{"rate", 48000}})}}),
                                 "{}", "rig.json");
    const atp::module_factory<deep_config_module> factory("deep_config");
    const atp::module_ptr m = factory.create(cfg);
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
    EXPECT_THROW((void)factory.create(atp::module_config{}), std::runtime_error);
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

struct sized_config : atp::config::fields {
    using fields::fields;
    std::int64_t& size = field("size", std::int64_t{16});
    std::string& device = field<std::string>("device");
};

class declared_config_module : public atp::module<atp::ports<>, "configured"> {
   public:
    using config_type = sized_config;
    explicit declared_config_module(const atp::module_config& cfg) : config_(cfg) {}

    [[nodiscard]] std::int64_t size() const {
        return config_.size;
    }

   private:
    sized_config config_;
};

}  // namespace

TEST(ModuleDeclaration, ASchemaTravelsWithTheDeclaration) {
    const atp::module_factory<declared_config_module> factory("configured");
    const atp::module_declaration decl = factory.declaration();

    ASSERT_TRUE(decl.config_schema.has_value());
    ASSERT_EQ(decl.config_schema->size(), 2u);
    EXPECT_EQ((*decl.config_schema)[0].name, "size");
    EXPECT_EQ((*decl.config_schema)[0].kind, atp::config::field_kind::integer);
    EXPECT_FALSE((*decl.config_schema)[0].required);
    EXPECT_EQ((*decl.config_schema)[1].name, "device");
    EXPECT_TRUE((*decl.config_schema)[1].required);
}

TEST(ModuleDeclaration, AModuleWithoutAConfigTypeDeclaresNoSchemaAtAll) {
    const atp::module_factory<never_constructed> factory("never_constructed");
    EXPECT_FALSE(factory.declaration().config_schema.has_value())
        << "no schema and an empty schema mean different things to an editor";
}

TEST(ModuleFactory, CreateRefusesAConfigTheModuleDeclaredAgainst) {
    const atp::module_factory<declared_config_module> factory("configured");
    const atp::module_config bad(atp::config::node::object({{"size", "big"}, {"nonsense", 1}}), "{}", "rig.json");

    try {
        (void)factory.create(bad);
        FAIL() << "a declared config is validated before the module exists";
    } catch (const atp::config::access_error& e) {
        const std::string text = e.what();
        EXPECT_NE(text.find("rig.json"), std::string::npos) << text;
        EXPECT_NE(text.find("size"), std::string::npos) << text;
        EXPECT_NE(text.find("nonsense"), std::string::npos) << text;
        EXPECT_NE(text.find("device"), std::string::npos) << text;
    }
}

TEST(ModuleFactory, CreateAcceptsAConfigThatSatisfiesTheDeclaration) {
    const atp::module_factory<declared_config_module> factory("configured");
    const atp::module_ptr m =
        factory.create(atp::module_config(atp::config::node::object({{"size", 32}, {"device", "hw:0"}})));

    ASSERT_NE(m, nullptr);
    EXPECT_EQ(dynamic_cast<declared_config_module&>(*m).size(), 32);
}
