// SPDX-License-Identifier: Apache-2.0
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include <atp/module.hpp>
#include <atp/module_factory.hpp>
#include <atp/module_registry.hpp>

namespace {

class plain_module : public atp::module<> {};

class config_reading_module : public atp::module<atp::io::ports<>, "config_reader", atp::version{1, 0}> {
   public:
    explicit config_reading_module(const atp::config_value& cfg) : channels_(cfg.int_at("channels", 0)) {}

    [[nodiscard]] std::int64_t channels() const {
        return channels_;
    }

   private:
    std::int64_t channels_;
};

class versioned_module : public atp::module<atp::io::ports<>, "", atp::version{2, 1}> {};

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
    atp::module_ptr module = factory.create(atp::config_value{});
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->get_version(), atp::version(2, 1));
    EXPECT_NE(factory.create(atp::config_value{}), module);
}

TEST(ModuleFactory, TypeErasedThroughBase) {
    atp::module_factory<plain_module> typed{"plain"};
    atp::module_factory_base& factory = typed;
    EXPECT_EQ(factory.name(), "plain");
    EXPECT_NE(factory.create(atp::config_value{}), nullptr);
}

TEST(ModuleFactory, CreateReturnsModulePtrWithEmptyPin) {
    atp::module_factory<bare_module> factory{"bare"};
    atp::module_ptr module = factory.create(atp::config_value{});
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module.get_deleter().pin, nullptr);
}

TEST(ModuleFactory, CreateBindsConstructorArgs) {
    atp::module_factory<configured_module, int> factory{"cfg", 42};
    atp::module_ptr first = factory.create(atp::config_value{});
    atp::module_ptr second = factory.create(atp::config_value{});
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_NE(first, second);
    EXPECT_EQ(dynamic_cast<configured_module&>(*first).value(), 42);
    EXPECT_EQ(dynamic_cast<configured_module&>(*second).value(), 42);
}

TEST(ModuleFactory, BoundArgsReachEveryInstance) {
    atp::module_factory<configured_module, int> factory("configured", 42);
    EXPECT_EQ(dynamic_cast<configured_module&>(*factory.create(atp::config_value{})).value(), 42);
}

TEST(ModuleFactory, ConfigReachesTheConstructor) {
    const atp::config_value cfg = atp::config_value::object({{"channels", 6}});
    const atp::module_factory<config_reading_module> factory("config_reader");
    const atp::module_ptr m = factory.create(cfg);
    EXPECT_EQ(dynamic_cast<config_reading_module&>(*m).channels(), 6);
}

TEST(ModuleFactory, ModuleThatIgnoresConfigIsBuiltUnchanged) {
    const atp::module_factory<configured_module, int> factory("configured", 42);
    EXPECT_EQ(dynamic_cast<configured_module&>(*factory.create(atp::config_value{})).value(), 42);
}
