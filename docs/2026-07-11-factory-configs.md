# Конфиги модулей через фабрики — план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Реализовать связывание конфига в точке регистрации по спеке `.superpowers/specs/2026-07-11-factory-configs-design.md`: `module_factory<M, TArgs...>` хранит аргументы конструктора, `add<M>(name, args...)` их принимает, `create()` не меняется.

**Architecture:** Кортеж аргументов в типизированной фабрике + `std::apply` в `create()`; `module_factory_base` и ABI не затронуты. `default_initializable` ослабляется до `constructible_from`.

**Tech Stack:** C++23 header-only, googletest, CMake ≥ 4.1 + Ninja, MSVC (CLion, профиль `cmake-build-debug`).

## Global Constraints

- Комментарии в коде — **по-русски**, объясняют «почему». Стиль — `docs/code_style.md`, `.clang-format` (120 колонок, обязательные скобки).
- Канонический include — `<atp/...>`; нейминг STL-style.
- **НЕ коммитить.** Пользователь коммитит сам. В конце задачи — предложить сообщение коммита и остановиться.
- Профиль `cmake-build-debug` — **MSVC + Ninja**; сборка/тесты одной командой через vcvars64:

```powershell
cmd /c '"C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 && cmake --build cmake-build-debug 2>&1 && ctest --test-dir cmake-build-debug 2>&1'
```

---

### Task 1: `module_factory<M, TArgs...>` + вариадик `add<M>`

**Files:**
- Modify: `include/atp/module_factory.hpp` (кортеж, `+<tuple>`, `+<utility>` уже есть)
- Modify: `include/atp/module_registry.hpp` (вариадик `add<M>` в `module_registry` и `module_registrar`)
- Test: `tests/module_factory_tests.cpp`, `tests/module_registry_tests.cpp`
- Modify: `.claude/CLAUDE.md` (строка про фабрики)

**Interfaces:**
- Produces: `module_factory<M, TArgs...>{name, args...}` (требование `std::constructible_from<M, const TArgs&...>`); `module_registry::add<M>(std::string name, TArgs&&... args)` и симметрично в `module_registrar`; `add<M>()` (has_module_name) и `module_factory<M>` без аргументов — прежняя семантика (пустой пак).

- [ ] **Step 1: Написать падающие тесты**

В `tests/module_factory_tests.cpp` — тип с конфигом в анонимный namespace (после `bare_module`):

```cpp
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
```

и тест в конец файла:

```cpp
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
```

В `tests/module_registry_tests.cpp` — тот же тип в анонимный namespace (после `handmade_module`):

```cpp
// Модуль с конфигом в конструкторе — для тестов вариадик-регистрации.
class configured_module : public atp::module<atp::io::inputs, atp::io::outputs> {
   public:
    explicit configured_module(int value) : value_(value) {}

    [[nodiscard]] int value() const {
        return value_;
    }

   private:
    int value_;
};
```

и тесты в конец файла:

```cpp
TEST(ModuleRegistry, AddBindsConstructorArgs) {
    atp::module_registry registry;
    registry.add<configured_module>("cfg", 7);
    auto module = registry.create("cfg");
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(dynamic_cast<configured_module&>(*module).value(), 7);
}

TEST(ModuleRegistry, SameTypeDifferentConfigsUnderAliases) {
    atp::module_registry registry;
    registry.add<configured_module>("slow", 10);
    registry.add<configured_module>("fast", 90);
    EXPECT_EQ(dynamic_cast<configured_module&>(*registry.create("slow")).value(), 10);
    EXPECT_EQ(dynamic_cast<configured_module&>(*registry.create("fast")).value(), 90);
}
```

- [ ] **Step 2: Убедиться, что не компилируется**

Сборка `atp_tests` (команда из Global Constraints, без ctest). Ожидание: ошибка вида `too many template arguments` / нет подходящего `add` с двумя аргументами.

- [ ] **Step 3: Реализация `include/atp/module_factory.hpp`**

Include-блок: добавить `<tuple>`. Шаблон заменить целиком:

```cpp
// Типизированная фабрика. Версию отдаёт статически из M::module_version —
// без создания экземпляра; модуль, написанный мимо шаблона module<> и не
// имеющий константы, получает default_version (та же семантика, что у
// module_base::get_version по умолчанию).
// Конфиг связывается при регистрации: фабрика хранит аргументы конструктора,
// каждый create() строит экземпляр от них — все экземпляры фабрики
// одинаковы, разные конфиги оформляются разными регистрациями (алиасами).
template <std::derived_from<module_base> M, typename... TArgs>
    requires std::constructible_from<M, const TArgs&...>
class module_factory final : public module_factory_base {
   public:
    explicit module_factory(std::string name, TArgs... args)
        : name_(std::move(name)), args_(std::move(args)...) {}

    [[nodiscard]] std::string_view name() const noexcept override {
        return name_;
    }

    [[nodiscard]] version get_version() const noexcept override {
        if constexpr (has_module_version<M>) {
            return M::module_version;
        } else {
            return default_version;
        }
    }

    [[nodiscard]] module_ptr create() const override {
        return std::apply([](const TArgs&... args) { return module_ptr(new M(args...), {}); }, args_);
    }

   private:
    std::string name_;
    std::tuple<TArgs...> args_;  // конфиг фабрики; create копирует его в экземпляр
};
```

- [ ] **Step 4: Реализация `include/atp/module_registry.hpp`**

В `module_registry` заменить сахарную перегрузку:

```cpp
    // Сахар: типовая фабрика. Имя задаётся здесь, в точке регистрации, —
    // один тип можно зарегистрировать под алиасами; args — конфиг
    // конструктора модуля, связывается с фабрикой.
    template <std::derived_from<module_base> M, typename... TArgs>
        requires std::constructible_from<M, const std::decay_t<TArgs>&...>
    module_factory_base& add(std::string name, TArgs&&... args) {
        return add(std::make_unique<module_factory<M, std::decay_t<TArgs>...>>(std::move(name),
                                                                               std::forward<TArgs>(args)...));
    }
```

Требование `std::default_initializable<M>` у `add<M>()` (обе версии — реестр и регистратор) заменить на `std::constructible_from<M>`; в `module_registrar` — та же вариадик-замена сахарной перегрузки. Include-блок реестра: `<type_traits>` (для `std::decay_t`), если отсутствует.

- [ ] **Step 5: Полная сборка + все тесты**

Команда из Global Constraints. Ожидание: 100% passed (154 + 3 = 157).

- [ ] **Step 6: CLAUDE.md**

В строке про `module_factory_base.hpp` / `module_factory.hpp` дополнить: «Конфиг конструктора связывается при регистрации: `add<M>(name, args...)` — фабрика хранит аргументы, все её экземпляры одинаковы; разные конфиги — разные регистрации.»

- [ ] **Step 7: Предложить пользователю коммит**

Сообщение: `module factories bind constructor args at registration`

---

## Self-review плана

- **Покрытие спеки:** шаблон с кортежем и `constructible_from` (Step 3); вариадик add в реестре и регистраторе + ослабление требования (Step 4); три теста спеки (Step 1: CreateBindsConstructorArgs, AddBindsConstructorArgs, SameTypeDifferentConfigsUnderAliases); ABI не тронут (create() без параметров — проверяется компиляцией плагин-демо в Step 5); CLAUDE.md (Step 6).
- **Типы согласованы:** `module_factory<configured_module, int>{"cfg", 42}` = сигнатура Step 3; `add<configured_module>("cfg", 7)` = вариадик Step 4; `module_ptr`/`dynamic_cast` — существующие контракты.
- **Плейсхолдеров нет.**
