#include <memory>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <atp/module.hpp>
#include <atp/module_registry.hpp>

namespace {

    class alpha_module
        : public atp::module<atp::io::inputs, atp::io::outputs, "", atp::version{1, 0}> {};

    class beta_module : public atp::module<atp::io::inputs, atp::io::outputs> {};

    // вторая версия того же по смыслу модуля — для тестов мультиверсионности
    class alpha_v2_module
        : public atp::module<atp::io::inputs, atp::io::outputs, "", atp::version{2, 0}> {};

    // второй безверсионный тип (default_version, как beta_module) — для
    // проверки, что дубликат определяется парой (имя, версия), а не типом
    class gamma_module : public atp::module<atp::io::inputs, atp::io::outputs> {};

    // модуль со встроенным именем — для add<M>() без аргумента
    class named_module
        : public atp::module<atp::io::inputs, atp::io::outputs, "named", atp::version{1, 0}> {};

    // модуль мимо шаблона module<>: контракт — статический член, не NTTP
    class handmade_module : public atp::module_base {
    public:
        static constexpr std::string_view module_name = "handmade";
        void initialize() override {}
        void start() override {}
        void iterate(std::stop_token) override {}
        void stop() override {}
    };

    // requires-выражение вне шаблона GCC проверяет жёстко (не SFINAE),
    // поэтому невозможность вызова проверяется через концепты: подстановка
    // параметра — уже immediate context, отказ разрешения даёт false.
    template <typename M>
    concept registry_adds_by_own_name =
        requires(atp::module_registry r) { r.add<M>(); };

    template <typename M>
    concept registrar_adds_by_own_name =
        requires(atp::module_registrar r) { r.add<M>(); };

} // namespace

// Анонимный модуль (module_name пуст) не проходит constraint add<M>()
// без аргумента — регистрация только под явным именем.
static_assert(!registry_adds_by_own_name<beta_module>);
static_assert(!registrar_adds_by_own_name<beta_module>);
static_assert(registry_adds_by_own_name<named_module>);

TEST(ModuleRegistry, AddAndCreate) {
    atp::module_registry registry;
    registry.add<alpha_module>("alpha");
    auto module = registry.create("alpha");
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->get_version(), atp::version(1, 0));
}

TEST(ModuleRegistry, AddReturnsFactoryReference) {
    atp::module_registry registry;
    atp::module_factory_base& factory = registry.add<alpha_module>("alpha");
    EXPECT_EQ(factory.name(), "alpha");
    EXPECT_EQ(factory.get_version(), atp::version(1, 0));
}

TEST(ModuleRegistry, SameNameDifferentVersionsCoexist) {
    atp::module_registry registry;
    registry.add<alpha_module>("proc");     // 1.0
    registry.add<alpha_v2_module>("proc");  // 2.0 — не дубликат
    EXPECT_EQ(registry.list().size(), 2u);
}

TEST(ModuleRegistry, DuplicateNameAndVersionThrows) {
    atp::module_registry registry;
    registry.add<alpha_module>("dup");
    try {
        registry.add<alpha_module>("dup");  // та же версия 1.0
        FAIL() << "expected std::runtime_error";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find("duplicate module 'dup' version '1.0'"),
                  std::string::npos);
    }
    // неудачная регистрация не портит существующую
    EXPECT_EQ(registry.at("dup").get_version(), atp::version(1, 0));
}

TEST(ModuleRegistry, DuplicateDefaultVersionThrows) {
    atp::module_registry registry;
    registry.add<beta_module>("dup");
    // другой тип, но та же default_version — дубликат пары (имя, версия)
    EXPECT_THROW(registry.add<gamma_module>("dup"), std::runtime_error);
}

TEST(ModuleRegistry, CreateWithoutVersionReturnsLatest) {
    atp::module_registry registry;
    registry.add<alpha_v2_module>("proc");  // регистрируем «не по порядку»
    registry.add<alpha_module>("proc");
    auto module = registry.create("proc");
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->get_version(), atp::version(2, 0));
}

TEST(ModuleRegistry, ListEnumeratesAllVersions) {
    atp::module_registry registry;
    registry.add<alpha_module>("proc");
    registry.add<alpha_v2_module>("proc");
    registry.add<beta_module>("beta");
    EXPECT_EQ(registry.list().size(), 3u);
}

TEST(ModuleRegistry, CreateExactVersion) {
    atp::module_registry registry;
    registry.add<alpha_module>("proc");     // 1.0
    registry.add<alpha_v2_module>("proc");  // 2.0
    auto module = registry.create("proc", atp::version(1, 0));
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->get_version(), atp::version(1, 0));
}

TEST(ModuleRegistry, CreateExactVersionIgnoresZeroPadding) {
    atp::module_registry registry;
    registry.add<alpha_module>("proc");  // 1.0
    // 1.0 == 1.0.0 — дополнение нулями, тот же ключ
    EXPECT_NE(registry.create("proc", atp::version(1, 0, 0)), nullptr);
}

TEST(ModuleRegistry, CreateMissingVersionThrows) {
    atp::module_registry registry;
    registry.add<alpha_module>("proc");
    try {
        (void)registry.create("proc", atp::version(9, 9));
        FAIL() << "expected std::runtime_error";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find("module 'proc' has no version '9.9'"),
                  std::string::npos);
    }
}

TEST(ModuleRegistry, CreateVersionOfUnknownNameThrows) {
    atp::module_registry registry;
    try {
        (void)registry.create("missing", atp::version(1, 0));
        FAIL() << "expected std::runtime_error";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find("no module named 'missing'"),
                  std::string::npos);
    }
}

TEST(ModuleRegistry, FindWithVersion) {
    atp::module_registry registry;
    registry.add<alpha_module>("proc");
    registry.add<alpha_v2_module>("proc");
    atp::module_factory_base* factory = registry.find("proc", atp::version(1, 0));
    ASSERT_NE(factory, nullptr);
    EXPECT_EQ(factory->get_version(), atp::version(1, 0));
    EXPECT_EQ(registry.find("proc", atp::version(9, 9)), nullptr);
    EXPECT_EQ(registry.find("missing", atp::version(1, 0)), nullptr);
}

TEST(ModuleRegistry, VersionsSortedAscending) {
    atp::module_registry registry;
    registry.add<alpha_v2_module>("proc");  // регистрируем «не по порядку»
    registry.add<alpha_module>("proc");
    EXPECT_EQ(registry.versions("proc"),
              (std::vector<atp::version>{atp::version(1, 0), atp::version(2, 0)}));
}

TEST(ModuleRegistry, VersionsUnknownNameEmpty) {
    atp::module_registry registry;
    EXPECT_TRUE(registry.versions("missing").empty());
}

TEST(ModuleRegistry, NullFactoryThrows) {
    atp::module_registry registry;
    EXPECT_THROW(registry.add(nullptr), std::invalid_argument);
}

TEST(ModuleRegistry, CreateUnknownThrows) {
    atp::module_registry registry;
    EXPECT_THROW((void)registry.create("missing"), std::runtime_error);
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

TEST(ModuleRegistry, RemoveVersionKeepsOthers) {
    atp::module_registry registry;
    registry.add<alpha_module>("proc");     // 1.0
    registry.add<alpha_v2_module>("proc");  // 2.0
    EXPECT_TRUE(registry.remove("proc", atp::version(2, 0)));
    // «последняя» пересчитана — осталась 1.0
    EXPECT_EQ(registry.at("proc").get_version(), atp::version(1, 0));
    // повторное снятие той же версии — false
    EXPECT_FALSE(registry.remove("proc", atp::version(2, 0)));
}

TEST(ModuleRegistry, RemoveLastVersionErasesName) {
    atp::module_registry registry;
    registry.add<alpha_module>("proc");
    EXPECT_TRUE(registry.remove("proc", atp::version(1, 0)));
    // имя без версий не должно находиться
    EXPECT_EQ(registry.find("proc"), nullptr);
    EXPECT_TRUE(registry.versions("proc").empty());
}

TEST(ModuleRegistry, RemoveNameErasesAllVersions) {
    atp::module_registry registry;
    registry.add<alpha_module>("proc");
    registry.add<alpha_v2_module>("proc");
    EXPECT_TRUE(registry.remove("proc"));
    EXPECT_FALSE(registry.remove("proc"));
    EXPECT_EQ(registry.find("proc"), nullptr);
}

TEST(ModuleRegistry, RemoveVersionOfUnknownNameReturnsFalse) {
    atp::module_registry registry;
    EXPECT_FALSE(registry.remove("missing", atp::version(1, 0)));
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
    registry.add(std::make_unique<atp::module_factory<beta_module>>("custom"));
    EXPECT_NE(registry.create("custom"), nullptr);
}

TEST(ModuleRegistrar, ForwardsToRegistry) {
    atp::module_registry registry;
    atp::module_registrar registrar{registry};
    registrar.add<alpha_module>("alpha");
    EXPECT_NE(registry.find("alpha"), nullptr);
}

TEST(ModuleRegistrar, RecordsNameVersionPairs) {
    atp::module_registry registry;
    atp::module_registrar registrar{registry};
    registrar.add<alpha_module>("alpha");
    registrar.add<beta_module>("beta");
    EXPECT_EQ(registrar.registered(),
              (std::vector<std::pair<std::string, atp::version>>{
                  {"alpha", atp::version(1, 0)},
                  {"beta", atp::default_version}}));
}

TEST(ModuleRegistry, AddWithoutNameUsesModuleName) {
    atp::module_registry registry;
    atp::module_factory_base& factory = registry.add<named_module>();
    EXPECT_EQ(factory.name(), "named");
    EXPECT_EQ(factory.get_version(), atp::version(1, 0));
    EXPECT_NE(registry.create("named"), nullptr);
}

TEST(ModuleRegistry, AliasOnTopOfOwnName) {
    atp::module_registry registry;
    registry.add<named_module>();
    registry.add<named_module>("alias");
    // обе записи живут независимо — собственное имя и алиас
    EXPECT_NE(registry.create("named"), nullptr);
    EXPECT_NE(registry.create("alias"), nullptr);
}

TEST(ModuleRegistrar, AddWithoutNameRecordsPair) {
    atp::module_registry registry;
    atp::module_registrar registrar{registry};
    registrar.add<named_module>();
    EXPECT_EQ(registrar.registered(),
              (std::vector<std::pair<std::string, atp::version>>{
                  {"named", atp::version(1, 0)}}));
}

TEST(ModuleRegistry, HandmadeModuleNameContract) {
    atp::module_registry registry;
    // контракт add<M>() — статический член module_name, а не сам шаблон
    registry.add<handmade_module>();
    EXPECT_NE(registry.create("handmade"), nullptr);
    EXPECT_EQ(registry.at("handmade").get_version(), atp::default_version);
}

TEST(ModuleRegistrar, FailedAddIsNotRecorded) {
    atp::module_registry registry;
    atp::module_registrar registrar{registry};
    registrar.add<alpha_module>("dup");
    // тот же тип — та же версия 1.0, дубликат пары (имя, версия)
    EXPECT_THROW(registrar.add<alpha_module>("dup"), std::runtime_error);
    EXPECT_EQ(registrar.registered(),
              (std::vector<std::pair<std::string, atp::version>>{
                  {"dup", atp::version(1, 0)}}));
}
