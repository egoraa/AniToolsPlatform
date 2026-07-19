#include <memory>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include <atp/module.hpp>
#include <atp/module_config.hpp>
#include <atp/module_factory.hpp>
#include <atp/module_registry.hpp>

namespace {

class plain_module : public atp::module<atp::io::inputs, atp::io::outputs> {};

class versioned_module : public atp::module<atp::io::inputs, atp::io::outputs, "", atp::version{2, 1}> {};

// Модуль мимо шаблона module<> — без константы module_version:
// фабрика должна отвечать default_version (симметрия с module_base).
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

   private:
    atp::io::inputs inputs_;
    atp::io::outputs outputs_;
};

// Модуль с конфигом в конструкторе — для тестов связывания аргументов фабрикой.
class configured_module : public atp::module<atp::io::inputs, atp::io::outputs> {
   public:
    explicit configured_module(int value) : value_(value) {}

    [[nodiscard]] int value() const {
        return value_;
    }

   private:
    int value_;
};

// Модуль с параметрами: конструктор берёт module_config первым аргументом.
class params_module : public atp::module<atp::io::inputs, atp::io::outputs, "configured"> {
   public:
    explicit params_module(atp::module_config config) : config_(std::move(config)) {}
    [[nodiscard]] const std::string& raw() const {
        return config_.raw;
    }

   private:
    atp::module_config config_;
};

// Параметры + связанные при регистрации аргументы фабрики.
class params_with_args_module : public atp::module<atp::io::inputs, atp::io::outputs, "configured_args"> {
   public:
    params_with_args_module(atp::module_config config, int bound) : config_(std::move(config)), bound_(bound) {}
    [[nodiscard]] const std::string& raw() const {
        return config_.raw;
    }
    [[nodiscard]] int bound() const {
        return bound_;
    }

   private:
    atp::module_config config_;
    int bound_;
};

// Пустой модуль с именем — для реестрового теста отказа от параметров.
class named_plain_module : public atp::module<atp::io::inputs, atp::io::outputs, "plain"> {};

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
    atp::module_ptr module = factory.create();
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

TEST(ModuleFactory, CreateReturnsModulePtrWithEmptyPin) {
    atp::module_factory<bare_module> factory{"bare"};
    atp::module_ptr module = factory.create();
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module.get_deleter().pin, nullptr);  // монолит: пин пуст
}

TEST(ModuleFactory, CreateBindsConstructorArgs) {
    atp::module_factory<configured_module, int> factory{"cfg", 42};
    atp::module_ptr first = factory.create();
    atp::module_ptr second = factory.create();
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_NE(first, second);  // независимые экземпляры от одного конфига
    EXPECT_EQ(dynamic_cast<configured_module&>(*first).value(), 42);
    EXPECT_EQ(dynamic_cast<configured_module&>(*second).value(), 42);
}

TEST(ModuleFactory, PassesConfigStringToModule) {
    atp::module_factory<params_module> factory("configured");
    atp::module_ptr m = factory.create(R"({"rate": 10})");
    EXPECT_EQ(static_cast<params_module&>(*m).raw(), R"({"rate": 10})");
}

TEST(ModuleFactory, ConfigComesFirstBoundArgsAfter) {
    atp::module_factory<params_with_args_module, int> factory("configured_args", 42);
    atp::module_ptr m = factory.create("cfg");
    EXPECT_EQ(static_cast<params_with_args_module&>(*m).raw(), "cfg");
    EXPECT_EQ(static_cast<params_with_args_module&>(*m).bound(), 42);
}

TEST(ModuleFactory, ParameterlessModuleRejectsNonEmptyConfig) {
    atp::module_registry registry;
    registry.add<params_module>();
    registry.add<named_plain_module>();

    EXPECT_EQ(static_cast<params_module&>(*registry.create("configured", R"("x")")).raw(), R"("x")");
    EXPECT_NO_THROW((void)registry.create("plain"));            // пустой конфиг — норма
    EXPECT_NO_THROW((void)registry.create("plain", ""));
    EXPECT_THROW((void)registry.create("plain", "{}"), std::runtime_error);  // параметры некому принять
}
