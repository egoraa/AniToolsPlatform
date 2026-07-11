#include <memory>
#include <stop_token>

#include <gtest/gtest.h>

#include <atp/module.hpp>
#include <atp/module_factory.hpp>

namespace {

class plain_module : public atp::module<atp::io::inputs, atp::io::outputs> {};

class versioned_module : public atp::module<atp::io::inputs, atp::io::outputs, "", atp::version{2, 1}> {};

// Модуль мимо шаблона module<> — без константы module_version:
// фабрика должна отвечать default_version (симметрия с module_base).
class bare_module : public atp::module_base {
   public:
    void initialize(atp::module_context&) override {}
    void start(atp::module_context&) override {}
    void iterate(std::stop_token) override {}
    void stop(atp::module_context&) override {}
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
    std::unique_ptr<atp::module_base> module = factory.create();
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->get_version(), atp::version(2, 1));
    // каждый вызов create — новый экземпляр
    EXPECT_NE(factory.create(), module);
}

TEST(ModuleFactory, TypeErasedThroughBase) {
    atp::module_factory<plain_module> typed{"plain"};
    atp::module_factory_base& factory = typed;
    EXPECT_EQ(factory.name(), "plain");
    EXPECT_NE(factory.create(), nullptr);
}
