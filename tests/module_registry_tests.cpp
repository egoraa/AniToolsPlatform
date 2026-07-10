#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <atp/module.hpp>
#include <atp/module_registry.hpp>

namespace {

    class alpha_module
        : public atp::module<atp::io::inputs, atp::io::outputs, atp::version{1, 0}> {};

    class beta_module : public atp::module<atp::io::inputs, atp::io::outputs> {};

} // namespace

TEST(ModuleRegistry, AddAndCreate) {
    atp::module_registry registry;
    registry.add<alpha_module>("alpha");
    auto module = registry.create("alpha");
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->get_version(), atp::version(1, 0));
}

TEST(ModuleRegistry, AddReturnsFactoryReference) {
    atp::module_registry registry;
    atp::module_factory& factory = registry.add<alpha_module>("alpha");
    EXPECT_EQ(factory.name(), "alpha");
    EXPECT_EQ(factory.get_version(), atp::version(1, 0));
}

TEST(ModuleRegistry, DuplicateNameThrows) {
    atp::module_registry registry;
    registry.add<alpha_module>("dup");
    EXPECT_THROW(registry.add<beta_module>("dup"), std::runtime_error);
    // неудачная регистрация не портит существующую
    EXPECT_EQ(registry.at("dup").get_version(), atp::version(1, 0));
}

TEST(ModuleRegistry, NullFactoryThrows) {
    atp::module_registry registry;
    EXPECT_THROW(registry.add(nullptr), std::invalid_argument);
}

TEST(ModuleRegistry, CreateUnknownThrows) {
    atp::module_registry registry;
    EXPECT_THROW(registry.create("missing"), std::runtime_error);
}

TEST(ModuleRegistry, FindReturnsNullptrForUnknown) {
    atp::module_registry registry;
    EXPECT_EQ(registry.find("missing"), nullptr);
}

TEST(ModuleRegistry, RemoveErasesFactory) {
    atp::module_registry registry;
    registry.add<alpha_module>("alpha");
    EXPECT_TRUE(registry.remove("alpha"));
    EXPECT_FALSE(registry.remove("alpha"));
    EXPECT_EQ(registry.find("alpha"), nullptr);
}

TEST(ModuleRegistry, ListEnumeratesFactories) {
    atp::module_registry registry;
    registry.add<alpha_module>("alpha");
    registry.add<beta_module>("beta");
    EXPECT_EQ(registry.list().size(), 2u);
}

TEST(ModuleRegistry, AliasesShareType) {
    atp::module_registry registry;
    registry.add<alpha_module>("alpha");
    registry.add<alpha_module>("alias");
    EXPECT_NE(registry.create("alias"), nullptr);
}

TEST(ModuleRegistry, CustomFactoryThroughGeneralPath) {
    atp::module_registry registry;
    registry.add(std::make_unique<atp::typed_module_factory<beta_module>>("custom"));
    EXPECT_NE(registry.create("custom"), nullptr);
}

TEST(ModuleRegistrar, ForwardsToRegistry) {
    atp::module_registry registry;
    atp::module_registrar registrar{registry};
    registrar.add<alpha_module>("alpha");
    EXPECT_NE(registry.find("alpha"), nullptr);
}

TEST(ModuleRegistrar, RecordsRegisteredNames) {
    atp::module_registry registry;
    atp::module_registrar registrar{registry};
    registrar.add<alpha_module>("alpha");
    registrar.add<beta_module>("beta");
    EXPECT_EQ(registrar.registered(), (std::vector<std::string>{"alpha", "beta"}));
}

TEST(ModuleRegistrar, FailedAddIsNotRecorded) {
    atp::module_registry registry;
    atp::module_registrar registrar{registry};
    registrar.add<alpha_module>("dup");
    EXPECT_THROW(registrar.add<beta_module>("dup"), std::runtime_error);
    EXPECT_EQ(registrar.registered(), (std::vector<std::string>{"dup"}));
}
