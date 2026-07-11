# Платформа исполнения (group/pipeline/pipeline_runner) — план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Реализовать платформу исполнения модулей: `atp::group` (структура), `atp::pipeline` (корень), `atp::pipeline_runner` (пул потоков с назначениями, жизненный цикл, ошибки). План самодостаточен (исходная спека не сохранилась); решения поздних ревизий — в `.superpowers/specs/2026-07-11-runner-stop-wait-design.md`, `...-iterate-idle-design.md`, `...-input-pull-model-design.md`, `...-plugin-pinning-design.md`.

**Architecture:** Группа — владеющий структурный узел (модули + подгруппы + экспорт портов как алиасы + записи соединений). Раннер строит карту «группа → поток» из таблицы назначений (невыделенная группа — inline у ближайшего назначенного предка, корень → поток 0), валидирует `safe`-входы на межпоточных соединениях, гоняет каскады `initialize/start/stop` и кооперативные циклы потоков с backoff простоя (busy/idle от `iterate`).

**Tech Stack:** C++23 header-only, googletest, CMake ≥ 4.1 + Ninja, MSVC (CLion, профиль `cmake-build-debug`).

## Global Constraints

- Комментарии в коде — **по-русски**, объясняют «почему», не механику. Стиль — `docs/code_style.md`, `.clang-format` (Chromium base, 4 пробела, 120 колонок, обязательные фигурные скобки).
- Нейминг STL-style: snake_case; члены с `value_`; `_base` для type-erased баз; `T`-префикс у шаблонных параметров; gtest-сьюты PascalCase.
- Канонический include — `<atp/...>` везде, и из `include/`, и из `src/`.
- Header guards: `ANITOOLSPLATFORM_<PATH>_HPP` (в `src/` — тот же формат, без различий).
- **НЕ коммитить.** Пользователь коммитит сам. В конце каждой задачи — предложить пользователю коммит с готовым сообщением и остановиться до его решения (или продолжать по его указанию без коммита).
- Профиль `cmake-build-debug` сконфигурирован под **MSVC + Ninja**. Сборка/тесты из shell — только в окружении VS, одной командой через vcvars64 (иначе cl.exe не находит стандартные заголовки):

```powershell
cmd /c '"C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 && cmake --build cmake-build-debug --target atp_tests 2>&1 && cmake-build-debug\tests\atp_tests.exe --gtest_filter="Group.*" 2>&1'
```

Команды в задачах ниже показаны без обёртки — исполнять их в этом окружении.

- Новые заголовки в `include/atp/` попадают в IDE-глоб автоматически, но **не** добавляются в umbrella `include/atp/io.hpp` (платформа — не io-слой). Для `src/` глоб появится в задаче 3.
- `(void)x` вместо `std::ignore`.

---

### Task 1: `io_base::thread_safe()`

Аксессор потокобезопасности экземпляра — нужен раннеру для валидации межпоточных соединений.

**Files:**
- Modify: `include/atp/io/io_base.hpp` (поле `locking_` уже есть, строки 39–47)
- Test: `tests/io_tests.cpp` (добавить сьют `IoBase`)

**Interfaces:**
- Produces: `[[nodiscard]] bool io_base::thread_safe() const noexcept` — true ⇔ экземпляр создан с тегом `safe`.

- [ ] **Step 1: Написать падающий тест**

В конец `tests/io_tests.cpp`:

```cpp
TEST(IoBase, ThreadSafeReflectsConstructionTag) {
    atp::io::input<int> guarded("guarded");                       // safe — умолчание
    atp::io::input<int> bare("bare", atp::io::unsafe);
    atp::io::output<int> guarded_out("out");
    EXPECT_TRUE(guarded.thread_safe());
    EXPECT_FALSE(bare.thread_safe());
    EXPECT_TRUE(guarded_out.thread_safe());
}
```

- [ ] **Step 2: Убедиться, что тест не компилируется**

```powershell
cmake --build cmake-build-debug --target atp_tests
```
Ожидание: ошибка компиляции `'thread_safe' ... no member`.

- [ ] **Step 3: Реализация**

В `include/atp/io/io_base.hpp` рядом с `name()/type()`:

```cpp
    // Потокобезопасность — свойство экземпляра (см. safety); раннер по ней
    // валидирует соединения, пересекающие границу потоков.
    [[nodiscard]] bool thread_safe() const noexcept {
        return locking_;
    }
```

- [ ] **Step 4: Тест зелёный**

```powershell
cmake --build cmake-build-debug --target atp_tests; cmake-build-debug\tests\atp_tests.exe --gtest_filter='IoBase.*'
```
Ожидание: `[  PASSED  ] 1 test`.

- [ ] **Step 5: Предложить пользователю коммит**

Сообщение: `add io_base::thread_safe() accessor`

---

### Task 2: `module_base::inputs()/outputs()` + `work_status` из `iterate` + `plugin_abi` → 3

Type-erased доступ к io-реестрам через базу — без него `group::connect` по именам невозможен. Ковариантный override в `module<>` — API авторов не меняется. Той же ABI-волной — контракт простоя: `iterate` возвращает `work_status` (busy/idle), по нему раннер (Task 8) сбавляет темп простаивающих потоков (спека `.superpowers/specs/2026-07-11-iterate-idle-design.md`).

**Files:**
- Modify: `include/atp/module_base.hpp`
- Modify: `include/atp/module.hpp` (добавить `override` к `inputs()/outputs()`)
- Modify: `include/atp/plugin.hpp` (abi уже 3 после pull-модели — дополнить комментарий)
- Modify: `tests/module_registry_tests.cpp:31` (`handmade_module`), `tests/module_factory_tests.cpp:17` (`bare_module`) — реализовать новые виртуалы, `iterate` → idle
- Modify: `examples/plugin_demo/counter_modules.hpp` (`iterate` → busy: счётчик работает на каждом вызове)
- Test: `tests/module_tests.cpp`

**Interfaces:**
- Produces: `virtual io::inputs& module_base::inputs() = 0;` + const-вариант; симметрично `outputs()`. `module<TInputs, TOutputs, ...>::inputs()` — ковариантный override, возвращает `TInputs&` как раньше.
- Produces: алиас `atp::work_status` (`= io::work_status`, сам enum уже в `io/threading.hpp`) и `virtual work_status iterate(std::stop_token) = 0` — busy: на вызове была работа; idle: делать нечего. Дефолт в `module<>` — idle. Возврат намеренно не `[[nodiscard]]`: одиночный тик в plugin_demo-хостах легитимно игнорирует статус.

- [ ] **Step 1: Написать падающий тест**

В `tests/module_tests.cpp` (типы модулей с портами в файле уже есть — использовать локальный, если нет):

```cpp
namespace {
struct erased_probe_inputs : atp::io::inputs {
    atp::io::input<int>& number = make<atp::io::input<int>>("number");
};
class erased_probe : public atp::module<erased_probe_inputs, atp::io::outputs> {};
}  // namespace

TEST(Module, IoRegistriesReachableThroughBase) {
    erased_probe m;
    atp::module_base& base = m;
    // Реестры через type-erased базу — те же объекты, что у конкретного типа.
    EXPECT_EQ(&base.inputs(), static_cast<atp::io::inputs*>(&m.inputs()));
    EXPECT_NE(base.inputs().find("number"), nullptr);
    EXPECT_EQ(base.outputs().list().size(), 0u);
}

TEST(Module, DefaultIterateReportsIdle) {
    erased_probe m;
    // Умолчание module<>: no-op-итерация и есть простой.
    EXPECT_EQ(m.iterate(std::stop_token{}), atp::work_status::idle);
}
```

- [ ] **Step 2: Убедиться, что не компилируется** (та же команда сборки; ошибка `no member named 'inputs' in 'atp::module_base'`).

- [ ] **Step 3: Реализация**

`include/atp/module_base.hpp` — forward-объявления и чистые виртуалы (полный include io не нужен, ковариантность проверяется в точке override):

```cpp
namespace atp::io {
class inputs;
class outputs;
}  // namespace atp::io
```

в класс `module_base`:

```cpp
    // Type-erased доступ к io-реестрам: машинерия соединений (group)
    // работает с модулем через module_ptr и без этих
    // аксессоров не видела бы портов. module<> реализует их ковариантным
    // override — авторам модулей ничего делать не нужно.
    [[nodiscard]] virtual io::inputs& inputs() = 0;
    [[nodiscard]] virtual const io::inputs& inputs() const = 0;
    [[nodiscard]] virtual io::outputs& outputs() = 0;
    [[nodiscard]] virtual const io::outputs& outputs() const = 0;
```

там же — контракт простоя: `work_status` уже объявлен в `<atp/io/threading.hpp>` (pull-модель входов), `module_base.hpp` добавляет `#include <atp/io/threading.hpp>` и поднимает имя:

```cpp
using work_status = io::work_status;  // сигнатура iterate пишется atp::work_status
```

и смена сигнатуры в классе:

```cpp
    virtual work_status iterate(std::stop_token stop_token) = 0;
```

`include/atp/module.hpp` — пометить существующие четыре метода `override`:

```cpp
    [[nodiscard]] TInputs& inputs() override {
        return inputs_;
    }
    [[nodiscard]] const TInputs& inputs() const override {
        return inputs_;
    }
    [[nodiscard]] TOutputs& outputs() override {
        return outputs_;
    }
    [[nodiscard]] const TOutputs& outputs() const override {
        return outputs_;
    }
```

там же — дефолт iterate:

```cpp
    work_status iterate(std::stop_token) override {
        return work_status::idle;  // no-op-итерация и есть простой
    }
```

`include/atp/plugin.hpp`:

```cpp
// 2: initialize/start/stop принимают module_context&.
// 3: pull-модель входов (io: -when/+take/watcher); module_base отдаёт
//    io-реестры (inputs()/outputs()); iterate возвращает work_status
//    (контракт простоя для раннера).
inline constexpr unsigned plugin_abi = 3;
```

`tests/module_registry_tests.cpp` (`handmade_module`) и `tests/module_factory_tests.cpp` (`bare_module`) — добавить в каждый:

```cpp
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
```

(если в файле нет `#include <atp/io.hpp>` — добавить). Там же `iterate` меняет возврат: `atp::work_status iterate(std::stop_token) override { return atp::work_status::idle; }`. В `examples/plugin_demo/counter_modules.hpp` `iterate` возвращает `atp::work_status::busy` — счётчик работает на каждом вызове. Хосты `host_static.cpp`/`host_dynamic.cpp` не правятся: возврат не `[[nodiscard]]`.

- [ ] **Step 4: Полный прогон**

```powershell
cmake --build cmake-build-debug; ctest --test-dir cmake-build-debug
```
Ожидание: 100% tests passed (тесты ABI-рукопожатия используют константу `plugin_abi` — перекомпилируются согласованно).

- [ ] **Step 5: Предложить пользователю коммит**

Сообщение: `module_base: io registries + work_status iterate (plugin ABI 3)`

---

### Task 3: цель `atp_host` + переезд `module_loader.hpp` в `src/`

`include/` остаётся SDK автора модулей; хост-машинерия живёт в `src/`.

**Files:**
- Move: `include/atp/module_loader.hpp` → `src/atp/module_loader.hpp` (содержимое без изменений, `git mv`)
- Modify: `CMakeLists.txt` (цель `atp_host`, глоб `src/`)
- Modify: `tests/CMakeLists.txt`, `examples/plugin_demo/CMakeLists.txt` (линковка потребителей загрузчика)

**Interfaces:**
- Produces: CMake-цель `atp_host` (INTERFACE): include-путь `src/`, линкует `atp_platform`. Все последующие задачи кладут заголовки в `src/atp/` и тестируются через `atp_tests` (он линкует `atp_host`).

- [ ] **Step 1: Переместить файл**

```powershell
git mv include/atp/module_loader.hpp src/atp/module_loader.hpp
```

- [ ] **Step 2: CMake**

`CMakeLists.txt` — после блока `atp_platform`:

```cmake
# Хост-платформа: исполнение пайплайна и загрузка плагинов. Отдельная цель,
# чтобы SDK автора модулей (atp_platform) не тянул хост-машинерию: плагины
# видят только include/, хост — include/ + src/.
add_library(atp_host INTERFACE)
target_include_directories(atp_host INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(atp_host INTERFACE atp_platform)

file(GLOB_RECURSE ATP_HOST_HEADERS CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/atp/*.hpp)
```

`tests/CMakeLists.txt`:

```cmake
target_link_libraries(atp_tests PRIVATE atp_host GTest::gtest_main ${CMAKE_DL_LIBS})
```

`examples/plugin_demo/CMakeLists.txt` — только динамический хост (плагин и статический хост остаются на SDK):

```cmake
target_link_libraries(atp_host_dynamic PRIVATE atp_host ${CMAKE_DL_LIBS})
```

- [ ] **Step 3: Полная сборка + все тесты**

```powershell
cmake --build cmake-build-debug; ctest --test-dir cmake-build-debug
```
Ожидание: конфигурация перезапустится (CONFIGURE_DEPENDS), 100% passed. Отдельно проверить, что `atp_demo_plugin` собрался без `src/` в include-путях (сборка это и докажет: цель не линкует `atp_host`).

- [ ] **Step 4: Предложить пользователю коммит**

Сообщение: `move module_loader to src/: host machinery leaves the module SDK`

---

### Task 4: `atp::group` — состав (владение, имена, порядок)

**Files:**
- Create: `src/atp/group.hpp`
- Create: `tests/group_tests.cpp` (+ добавить в `tests/CMakeLists.txt` в список сорцов `atp_tests`)

**Interfaces:**
- Produces:
  - `explicit group(std::string name)`; `const std::string& name() const`
  - `module_base& add(std::string name, module_ptr)` — бросает `std::invalid_argument` (null/пустое имя), `std::runtime_error` (дубликат); `module_ptr` несёт пин библиотеки — модуль плагина в группе автоматически держит свою DLL
  - `template <M, TArgs...> M& make(std::string name, TArgs&&...)` и `template <M, TArgs...> M& make(TArgs&&...)` (имя из `M::module_name`, контракт `has_module_name`)
  - `group& add_group(std::string name)`
  - `struct element { std::string name; module_ptr module; std::unique_ptr<group> subgroup; }` — ровно одно из двух ненулевое; `const std::vector<element>& elements() const` — порядок вставки
  - `module_base* find_module(const std::string&) const`, `group* find_group(const std::string&) const` — nullptr, если нет

- [ ] **Step 1: Написать падающие тесты**

`tests/group_tests.cpp`:

```cpp
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include <atp/group.hpp>
#include <atp/module.hpp>

namespace {

class named_module : public atp::module<atp::io::inputs, atp::io::outputs, "named"> {};
class plain_module : public atp::module<atp::io::inputs, atp::io::outputs> {};

TEST(Group, OwnsModulesAndSubgroupsInInsertionOrder) {
    atp::group g("root");
    g.make<named_module>();                       // имя из типа
    atp::group& sub = g.add_group("sub");
    g.make<plain_module>("tail");                 // имя в точке регистрации

    ASSERT_EQ(g.elements().size(), 3u);
    EXPECT_EQ(g.elements()[0].name, "named");
    EXPECT_NE(g.elements()[0].module, nullptr);
    EXPECT_EQ(g.elements()[1].subgroup.get(), &sub);
    EXPECT_EQ(g.elements()[2].name, "tail");
    EXPECT_EQ(sub.name(), "sub");
}

TEST(Group, AddAcceptsPrebuiltModule) {
    atp::group g("root");
    atp::module_base& m = g.add("ready", atp::module_ptr{new plain_module});  // в т.ч. из module_registry::create()
    EXPECT_EQ(g.find_module("ready"), &m);
    EXPECT_EQ(g.find_module("missing"), nullptr);
    EXPECT_EQ(g.find_group("ready"), nullptr);    // это модуль, не группа
}

TEST(Group, RejectsDuplicateAndEmptyNames) {
    atp::group g("root");
    g.make<plain_module>("one");
    EXPECT_THROW(g.make<plain_module>("one"), std::runtime_error);
    EXPECT_THROW(g.add_group("one"), std::runtime_error);   // общее пространство имён
    EXPECT_THROW(g.make<plain_module>(""), std::invalid_argument);
    EXPECT_THROW(g.add("null", nullptr), std::invalid_argument);
}

}  // namespace
```

`tests/CMakeLists.txt` — добавить `group_tests.cpp` в `add_executable(atp_tests ...)`.

- [ ] **Step 2: Убедиться, что не компилируется** (нет `<atp/group.hpp>`).

- [ ] **Step 3: Реализация `src/atp/group.hpp`**

```cpp
#ifndef ANITOOLSPLATFORM_GROUP_HPP
#define ANITOOLSPLATFORM_GROUP_HPP

#include <concepts>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <atp/module_base.hpp>
#include <atp/module_registry.hpp>

namespace atp {

// Структурный узел пайплайна: владеет модулями и подгруппами, ведёт экспорт
// портов и соединения. НЕ единица исполнения: как группа исполняется (свой
// поток пула или inline у предка), решает раскладка раннера — вложенность
// здесь только инкапсуляция. НЕ потокобезопасен: фаза настройки, как io-реестры.
class group {
   public:
    // Элемент состава: ровно один из указателей ненулевой. Модули и подгруппы
    // в одном списке: порядок вставки — это порядок каскадов и iterate,
    // он должен быть сквозным, а не «сначала модули, потом группы».
    struct element {
        std::string name;
        module_ptr module;  // пин библиотеки едет в делетере — модуль плагина держит свою DLL
        std::unique_ptr<group> subgroup;
    };

    explicit group(std::string name) : name_(std::move(name)) {}

    group(const group&) = delete;
    group& operator=(const group&) = delete;

    [[nodiscard]] const std::string& name() const {
        return name_;
    }

    // Приём готового модуля — в т.ч. созданного module_registry::create().
    module_base& add(std::string name, module_ptr module) {
        if (!module) {
            throw std::invalid_argument("null module for '" + name + "' in group '" + name_ + "'");
        }
        ensure_unique(name);
        elements_.push_back({std::move(name), std::move(module), nullptr});
        return *elements_.back().module;
    }

    // Сахар: создать модуль на месте (владеющий контейнер). Аргументы —
    // конструктору модуля; io-элементы, в отличие от модулей, создаются
    // с (name, safety), поэтому здесь perfect forwarding, а не их контракт.
    template <std::derived_from<module_base> M, typename... TArgs>
        requires std::constructible_from<M, TArgs...>
    M& make(std::string name, TArgs&&... args) {
        module_ptr module(new M(std::forward<TArgs>(args)...), {});  // монолит: пин пуст
        M& ref = static_cast<M&>(*module);
        add(std::move(name), std::move(module));
        return ref;
    }

    // Имя из самого модуля — тот же контракт has_module_name, что в реестре
    // фабрик. При коллизии перегрузок (конструктор модуля начинается со
    // строки) — указывать имя явно.
    template <std::derived_from<module_base> M, typename... TArgs>
        requires has_module_name<M> && std::constructible_from<M, TArgs...>
    M& make(TArgs&&... args) {
        return make<M>(std::string{M::module_name}, std::forward<TArgs>(args)...);
    }

    group& add_group(std::string name) {
        ensure_unique(name);
        auto child = std::make_unique<group>(name);
        group& ref = *child;
        elements_.push_back({std::move(name), nullptr, std::move(child)});
        return ref;
    }

    // Состав по порядку вставки — обход для раннера и машинерии соединений.
    [[nodiscard]] const std::vector<element>& elements() const {
        return elements_;
    }

    [[nodiscard]] module_base* find_module(const std::string& name) const {
        const element* e = find_element(name);
        return e ? e->module.get() : nullptr;
    }

    [[nodiscard]] group* find_group(const std::string& name) const {
        const element* e = find_element(name);
        return e ? e->subgroup.get() : nullptr;
    }

   private:
    [[nodiscard]] const element* find_element(const std::string& name) const {
        // линейный поиск: фаза настройки, составы групп невелики —
        // дублировать порядок вставки map-ом незачем
        for (const element& e : elements_) {
            if (e.name == name) {
                return &e;
            }
        }
        return nullptr;
    }

    void ensure_unique(const std::string& name) const {
        if (name.empty()) {
            throw std::invalid_argument("empty child name in group '" + name_ + "'");
        }
        if (find_element(name)) {
            throw std::runtime_error("duplicate name '" + name + "' in group '" + name_ + "'");
        }
    }

    std::string name_;
    std::vector<element> elements_;
};

}  // namespace atp

#endif  // ANITOOLSPLATFORM_GROUP_HPP
```

- [ ] **Step 4: Тесты зелёные**

```powershell
cmake --build cmake-build-debug --target atp_tests; cmake-build-debug\tests\atp_tests.exe --gtest_filter='Group.*'
```
Ожидание: `[  PASSED  ] 3 tests`.

- [ ] **Step 5: Предложить пользователю коммит**

Сообщение: `add atp::group: owning structural node (modules + nested groups)`

---

### Task 5: `group` — экспорт портов (алиасы с владельцем)

**Files:**
- Modify: `src/atp/group.hpp`
- Test: `tests/group_tests.cpp`

**Interfaces:**
- Consumes: `module_base::inputs()/outputs()` (Task 2), `io::inputs::find/at` (реестр), состав group (Task 4).
- Produces:
  - `void expose_input(std::string alias, const std::string& path)`; симметрично `expose_output` — только путь `"<дитя>.<порт>"`: владелец порта восстанавливается из состава группы по построению. Перегрузки по ссылке нет намеренно: принадлежность порта группе непроверяема, ошибка вызывающего молча искажала бы владельца и валидацию межпоточности
  - `io::input_base& input_at(const std::string& alias) const` / `io::output_base& output_at(const std::string& alias) const` — бросают
  - внутренние структуры: `struct exported_input { io::input_base* port; const group* owner; }` (симметрично output), доступ подгруппам при разрешении путей
  - разрешение путей `"<дитя>.<порт>"`: `resolved_input resolve_input(const std::string& path) const` → `{port, owner}` (для порта модуля owner = сама группа, для алиаса подгруппы — владелец из её таблицы); симметрично `resolve_output`. Приватные, но описаны здесь: Task 6 строит на них `connect`.

- [ ] **Step 1: Написать падающие тесты**

В `tests/group_tests.cpp` добавить (типы с портами):

```cpp
struct number_inputs : atp::io::inputs {
    atp::io::input<int>& number = make<atp::io::input<int>>("number");
};
struct number_outputs : atp::io::outputs {
    atp::io::output<int>& number = make<atp::io::output<int>>("number");
};
class source_module : public atp::module<atp::io::inputs, number_outputs> {};
class sink_module : public atp::module<number_inputs, atp::io::outputs> {};

TEST(Group, ExposesModulePortsUnderAlias) {
    atp::group g("root");
    sink_module& sink = g.make<sink_module>("sink");
    g.expose_input("in", "sink.number");
    source_module& src = g.make<source_module>("src");
    g.expose_output("out", "src.number");

    EXPECT_EQ(&g.input_at("in"), &sink.inputs().number);       // алиас — тот же объект
    EXPECT_EQ(&g.output_at("out"), &src.outputs().number);
}

TEST(Group, ReexportResolvesToRealPortImmediately) {
    atp::group root("root");
    atp::group& inner = root.add_group("inner");
    sink_module& sink = inner.make<sink_module>("sink");
    inner.expose_input("in", "sink.number");
    root.expose_input("outer_in", "inner.in");                 // ре-экспорт алиаса

    EXPECT_EQ(&root.input_at("outer_in"), &sink.inputs().number);
}

TEST(Group, ExposeErrors) {
    atp::group g("root");
    g.make<sink_module>("sink");
    g.expose_input("in", "sink.number");
    EXPECT_THROW(g.expose_input("in", "sink.number"), std::runtime_error);      // дубликат алиаса
    EXPECT_THROW(g.expose_input("x", "nobody.number"), std::runtime_error);     // нет такого дитя
    EXPECT_THROW(g.expose_input("y", "sink.missing"), std::runtime_error);      // нет такого порта
    EXPECT_THROW(g.expose_input("z", "sink"), std::invalid_argument);           // путь без точки
    EXPECT_THROW(g.input_at("missing"), std::runtime_error);
}
```

- [ ] **Step 2: Не компилируется / падает** (нет `expose_input`).

- [ ] **Step 3: Реализация**

В `group` добавить include `<atp/io.hpp>` и члены (после `find_group`, приватное — в конец):

```cpp
    // --- Экспорт портов: алиасы внутренних, граница только на этапе сборки ---
    // Владелец в записи — группа, чей модуль реально держит порт: по ней
    // раннер решает, пересекает ли соединение границу потоков. При
    // ре-экспорте владелец переносится как есть. Экспорт — только по пути
    // «<дитя>.<порт>»: владелец восстанавливается из состава по построению;
    // перегрузки по ссылке нет намеренно — принадлежность порта группе
    // непроверяема, ошибка вызывающего молча искажала бы валидацию.

    void expose_input(std::string alias, const std::string& path) {
        resolved_input r = resolve_input(path);
        add_exported(exported_inputs_, std::move(alias), r.port, r.owner, "input");
    }

    void expose_output(std::string alias, const std::string& path) {
        resolved_output r = resolve_output(path);
        add_exported(exported_outputs_, std::move(alias), r.port, r.owner, "output");
    }

    [[nodiscard]] io::input_base& input_at(const std::string& alias) const {
        auto it = exported_inputs_.find(alias);
        if (it == exported_inputs_.end()) {
            throw std::runtime_error("group '" + name_ + "' does not export input '" + alias + "'");
        }
        return *it->second.port;
    }

    [[nodiscard]] io::output_base& output_at(const std::string& alias) const {
        auto it = exported_outputs_.find(alias);
        if (it == exported_outputs_.end()) {
            throw std::runtime_error("group '" + name_ + "' does not export output '" + alias + "'");
        }
        return *it->second.port;
    }
```

приватная часть:

```cpp
    template <typename TPort>
    struct exported {
        TPort* port;
        const group* owner;
    };
    template <typename TPort>
    using export_table = std::unordered_map<std::string, exported<TPort>>;

    struct resolved_input {
        io::input_base* port;
        const group* owner;
    };
    struct resolved_output {
        io::output_base* port;
        const group* owner;
    };

    template <typename TPort>
    void add_exported(export_table<TPort>& table, std::string alias, TPort* port, const group* owner,
                      const char* kind) {
        if (alias.empty()) {
            throw std::invalid_argument("empty export alias in group '" + name_ + "'");
        }
        auto [it, inserted] = table.try_emplace(std::move(alias), exported<TPort>{port, owner});
        if (!inserted) {
            throw std::runtime_error("duplicate exported " + std::string(kind) + " '" + it->first + "' in group '" +
                                     name_ + "'");
        }
    }

    // Путь «<дитя>.<порт>» в области видимости группы: дитя-модуль отдаёт
    // порт из своего реестра, дитя-группа — из таблицы экспорта (владелец
    // переносится из записи — так ре-экспорт разрешается сразу в реальный порт).
    [[nodiscard]] std::pair<const element*, std::string> split_path(const std::string& path) const {
        auto dot = path.find('.');
        if (dot == std::string::npos || dot == 0 || dot + 1 == path.size()) {
            throw std::invalid_argument("path '" + path + "' in group '" + name_ + "': expected '<child>.<port>'");
        }
        const element* e = find_element(path.substr(0, dot));
        if (!e) {
            throw std::runtime_error("group '" + name_ + "' has no child '" + path.substr(0, dot) + "'");
        }
        return {e, path.substr(dot + 1)};
    }

    [[nodiscard]] resolved_input resolve_input(const std::string& path) const {
        auto [e, port_name] = split_path(path);
        if (e->module) {
            io::input_base* port = e->module->inputs().find(port_name);
            if (!port) {
                throw std::runtime_error("module '" + e->name + "' has no input '" + port_name + "'");
            }
            return {port, this};
        }
        auto it = e->subgroup->exported_inputs_.find(port_name);
        if (it == e->subgroup->exported_inputs_.end()) {
            throw std::runtime_error("group '" + e->name + "' does not export input '" + port_name + "'");
        }
        return {it->second.port, it->second.owner};
    }

    [[nodiscard]] resolved_output resolve_output(const std::string& path) const {
        auto [e, port_name] = split_path(path);
        if (e->module) {
            io::output_base* port = e->module->outputs().find(port_name);
            if (!port) {
                throw std::runtime_error("module '" + e->name + "' has no output '" + port_name + "'");
            }
            return {port, this};
        }
        auto it = e->subgroup->exported_outputs_.find(port_name);
        if (it == e->subgroup->exported_outputs_.end()) {
            throw std::runtime_error("group '" + e->name + "' does not export output '" + port_name + "'");
        }
        return {it->second.port, it->second.owner};
    }

    export_table<io::input_base> exported_inputs_;
    export_table<io::output_base> exported_outputs_;
```

Include-блок дополнить: `<unordered_map>`, `<utility>`, `<atp/io.hpp>`.

- [ ] **Step 4: Тесты зелёные** (`--gtest_filter='Group.*'`, `[  PASSED  ] 6 tests`).

- [ ] **Step 5: Предложить пользователю коммит**

Сообщение: `group: port export as aliases with owner tracking`

---

### Task 6: `group` — соединения по путям + авторазрыв

**Files:**
- Modify: `src/atp/group.hpp`
- Test: `tests/group_tests.cpp`

**Interfaces:**
- Consumes: `resolve_input/resolve_output` (Task 5), `output_base::connect/disconnect` (io-слой).
- Produces:
  - `void connect(const std::string& from, const std::string& to)` / `... , io::replay_t)`
  - `struct connection { io::output_base* out; io::input_base* in; const group* out_owner; const group* in_owner; }`
  - `const std::vector<connection>& connections() const` — записи соединений, созданных ЭТОЙ группой
  - `~group()` рвёт эти соединения (`disconnect`) до разрушения детей

- [ ] **Step 1: Написать падающие тесты**

```cpp
TEST(Group, ConnectsByPathsAcrossSubgroupBoundary) {
    atp::group root("root");
    source_module& src = root.make<source_module>("src");
    atp::group& inner = root.add_group("inner");
    sink_module& sink = inner.make<sink_module>("sink");
    inner.expose_input("in", "sink.number");

    root.connect("src.number", "inner.in");
    ASSERT_EQ(root.connections().size(), 1u);
    EXPECT_EQ(root.connections()[0].out_owner, &root);    // src живёт в root
    EXPECT_EQ(root.connections()[0].in_owner, &inner);    // реальный вход — в inner

    src.outputs().number(7);
    EXPECT_EQ(sink.inputs().number.get(), 7);             // прямая доставка, без хопов
}

TEST(Group, ConnectReplayDeliversCache) {
    atp::group g("root");
    source_module& src = g.make<source_module>("src");
    sink_module& sink = g.make<sink_module>("sink");
    src.outputs().number(42);                             // кэш до подключения
    g.connect("src.number", "sink.number", atp::io::replay);
    EXPECT_EQ(sink.inputs().number.get(), 42);
}

TEST(Group, ConnectErrors) {
    atp::group g("root");
    g.make<source_module>("src");
    g.make<sink_module>("sink");
    EXPECT_THROW(g.connect("src.missing", "sink.number"), std::runtime_error);
    EXPECT_THROW(g.connect("nobody.number", "sink.number"), std::runtime_error);
}

TEST(Group, DestructorDisconnectsItsConnections) {
    source_module src;                                     // выход живёт дольше группы
    auto g = std::make_unique<atp::group>("root");
    sink_module& sink = g->make<sink_module>("sink");
    src.outputs().number.connect(sink.inputs().number);    // прямое, мимо группы — рвёт вызывающий
    src.outputs().number.disconnect(sink.inputs().number);

    g->expose_input("in", "sink.number");
    // соединение через группу: наружный выход → вход внутри группы
    src.outputs().number.connect(g->input_at("in"));       // это тоже мимо группы...
    src.outputs().number.disconnect(g->input_at("in"));

    // ...а вот запись группы: локальная пара внутри неё
    atp::group& sub = g->add_group("sub");
    (void)sub;
    source_module& inner_src = g->make<source_module>("inner_src");
    g->connect("inner_src.number", "sink.number");
    EXPECT_EQ(inner_src.outputs().number.connections(), 1u);

    g.reset();                                             // деструктор рвёт своё соединение
    // выхода уже нет — проверка на счётчике была до разрушения; сам факт
    // отсутствия краха при разрушении входа после группы и есть проверка
    EXPECT_EQ(src.outputs().number.connections(), 0u);
}
```

- [ ] **Step 2: Не компилируется** (нет `connect`).

- [ ] **Step 3: Реализация**

Публичное:

```cpp
    // --- Соединения: пути в области видимости группы ---
    // Запись хранит владельцев обеих сторон — материал для проверки
    // safe-входов раннером в start(), когда известна раскладка потоков.
    struct connection {
        io::output_base* out;
        io::input_base* in;
        const group* out_owner;
        const group* in_owner;
    };

    void connect(const std::string& from, const std::string& to) {
        link(from, to, false);
    }

    void connect(const std::string& from, const std::string& to, io::replay_t) {
        link(from, to, true);
    }

    [[nodiscard]] const std::vector<connection>& connections() const {
        return connections_;
    }

    // Тело деструктора выполняется до деструкторов членов: обе стороны
    // каждого записанного соединения ещё живы — правило «disconnect до
    // разрушения входа» соблюдается конструктивно.
    ~group() {
        for (const connection& c : connections_) {
            (void)c.out->disconnect(*c.in);
        }
    }
```

Приватное:

```cpp
    void link(const std::string& from, const std::string& to, bool deliver_cached) {
        resolved_output out = resolve_output(from);
        resolved_input in = resolve_input(to);
        if (deliver_cached) {
            out.port->connect(*in.port, io::replay);
        } else {
            out.port->connect(*in.port);
        }
        connections_.push_back({out.port, in.port, out.owner, in.owner});
    }

    std::vector<connection> connections_;
```

- [ ] **Step 4: Тесты зелёные** (`Group.*`, `[  PASSED  ] 10 tests`).

- [ ] **Step 5: Предложить пользователю коммит**

Сообщение: `group: name-based connect with connection records and auto-disconnect`

---

### Task 7: `atp::pipeline`

**Files:**
- Create: `src/atp/pipeline.hpp`
- Test: `tests/pipeline_tests.cpp` (+ в `tests/CMakeLists.txt`)

**Interfaces:**
- Produces: `pipeline()` (default); `group& root()` (+const); `service_directory& services()`; `module_context& context()`. Корневая группа называется `"root"`.

- [ ] **Step 1: Падающий тест**

`tests/pipeline_tests.cpp`:

```cpp
#include <gtest/gtest.h>

#include <atp/pipeline.hpp>

TEST(Pipeline, RootServicesAndContextAreWired) {
    atp::pipeline p;
    EXPECT_EQ(p.root().name(), "root");
    EXPECT_EQ(&p.context().services, &p.services());   // контекст собран из служб пайплайна
    p.root().add_group("stage");                       // корень — обычная группа
    EXPECT_NE(p.root().find_group("stage"), nullptr);
}
```

- [ ] **Step 2: Не компилируется.**

- [ ] **Step 3: Реализация `src/atp/pipeline.hpp`**

```cpp
#ifndef ANITOOLSPLATFORM_PIPELINE_HPP
#define ANITOOLSPLATFORM_PIPELINE_HPP

#include <atp/group.hpp>
#include <atp/module_context.hpp>
#include <atp/service_directory.hpp>

namespace atp {

// Корень композиции: корневая группа + платформенные службы. Чистая
// структура без потоков — исполнение целиком в pipeline_runner.
// Порядок членов = порядок разрушения в обратную сторону: группа (и её
// соединения/модули) умирает раньше служб, на которые модули могли
// ссылаться в stop().
class pipeline {
   public:
    pipeline() = default;

    pipeline(const pipeline&) = delete;
    pipeline& operator=(const pipeline&) = delete;

    [[nodiscard]] group& root() {
        return root_;
    }
    [[nodiscard]] const group& root() const {
        return root_;
    }
    [[nodiscard]] service_directory& services() {
        return services_;
    }
    [[nodiscard]] module_context& context() {
        return context_;
    }

   private:
    service_directory services_;
    module_context context_{services_};
    group root_{"root"};
};

}  // namespace atp

#endif  // ANITOOLSPLATFORM_PIPELINE_HPP
```

- [ ] **Step 4: Тест зелёный** (`Pipeline.*`).

- [ ] **Step 5: Предложить пользователю коммит**

Сообщение: `add atp::pipeline: composition root (group + services + context)`

---

### Task 8: `pipeline_runner` — пул, назначения, валидация, каскады, потоки

Самая большая задача: раннер целиком, кроме путей ошибок исполнения (Task 9).

**Files:**
- Create: `src/atp/pipeline_runner.hpp`
- Create: `tests/pipeline_test_support.hpp` (журнал + модуль-зонд)
- Create: `tests/pipeline_runner_tests.cpp` (+ в `tests/CMakeLists.txt`)

**Interfaces:**
- Consumes: `group::elements()/connections()` (Tasks 4–6), `pipeline::root()/context()` (Task 7), `io_base::thread_safe()` (Task 1), `module_base::iterate(std::stop_token)`.
- Produces:
  - `explicit pipeline_runner(std::size_t threads)` — `std::invalid_argument` при 0
  - `void assign(const group&, std::size_t thread_index)` — до `start`; `std::out_of_range` при index ≥ threads; `std::logic_error` после старта
  - `void start(pipeline&)` — шаги: карта потоков → валидация соединений → каскад `initialize` → каскад `start` → запуск потоков; `std::logic_error` при повторном старте
  - `void stop()` — идемпотентен, не бросает
  - `bool running() const`
  - (Task 9 добавит `wait()`, `error()`)
- Семантика: невыделенная группа наследует поток ближайшего назначенного предка; корень без назначения → поток 0; поток без групп не создаётся; inline-подгруппа итерируется в позиции своего порядка вставки; назначенная подгруппа предком пропускается; поток, чей пасс целиком idle, сбавляет темп — yield, затем сон с удвоением 1 мс → 10 мс (прерываемый стоп-токеном), busy-пасс сбрасывает темп (спека `.superpowers/specs/2026-07-11-iterate-idle-design.md`).

- [ ] **Step 1: Тестовая поддержка `tests/pipeline_test_support.hpp`**

```cpp
#ifndef ANITOOLSPLATFORM_TESTS_PIPELINE_TEST_SUPPORT_HPP
#define ANITOOLSPLATFORM_TESTS_PIPELINE_TEST_SUPPORT_HPP

#include <latch>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <atp/module.hpp>

namespace atp_tests {

// Журнал событий жизненного цикла — общий для теста и модулей-зондов.
// Потокобезопасен: iterate пишут потоки пула.
struct probe_event {
    std::string module;
    std::string phase;  // "initialize" / "start" / "iterate" / "stop"
    std::thread::id thread;
};

class event_log {
   public:
    void record(const std::string& module, const std::string& phase) {
        std::lock_guard lock(mutex_);
        events_.push_back({module, phase, std::this_thread::get_id()});
    }

    [[nodiscard]] std::vector<probe_event> snapshot() const {
        std::lock_guard lock(mutex_);
        return events_;
    }

    // Имена модулей в порядке событий заданной фазы — для проверки каскадов.
    [[nodiscard]] std::vector<std::string> order_of(const std::string& phase) const {
        std::vector<std::string> result;
        for (const probe_event& e : snapshot()) {
            if (e.phase == phase) {
                result.push_back(e.module);
            }
        }
        return result;
    }

    // Поток, писавший iterate модуля (первое событие); id{} — не итерировался.
    [[nodiscard]] std::thread::id iterate_thread(const std::string& module) const {
        for (const probe_event& e : snapshot()) {
            if (e.module == module && e.phase == "iterate") {
                return e.thread;
            }
        }
        return {};
    }

   private:
    mutable std::mutex mutex_;
    std::vector<probe_event> events_;
};

// Модуль-зонд: пишет фазы в журнал, по указанию бросает из фазы,
// сигналит latch-ем о первом iterate — тесты ждут без sleep.
class probe_module : public atp::module<atp::io::inputs, atp::io::outputs> {
   public:
    probe_module(event_log& log, std::string name) : log_(&log), name_(std::move(name)) {}

    [[nodiscard]] std::string_view get_name() const noexcept override {
        return name_;
    }

    std::latch* first_iterate = nullptr;  // не владеет; nullptr — не сигналить
    std::string throw_in;                 // фаза, из которой бросить (после записи в журнал)

    void initialize(atp::module_context&) override {
        hit("initialize");
    }
    void start(atp::module_context&) override {
        hit("start");
    }
    atp::work_status iterate(std::stop_token) override {
        if (first_iterate && !signaled_) {
            signaled_ = true;
            first_iterate->count_down();
        }
        hit("iterate");
        return atp::work_status::busy;  // журнал пишется каждый вызов — это работа
    }
    void stop(atp::module_context&) override {
        hit("stop");
    }

   private:
    void hit(const char* phase) {
        log_->record(name_, phase);
        if (throw_in == phase) {
            throw std::runtime_error(name_ + ": failure in " + phase);
        }
    }

    event_log* log_;
    std::string name_;
    bool signaled_ = false;  // latch сигналится один раз; поле читает только поток группы
};

}  // namespace atp_tests

#endif  // ANITOOLSPLATFORM_TESTS_PIPELINE_TEST_SUPPORT_HPP
```

- [ ] **Step 2: Падающие тесты `tests/pipeline_runner_tests.cpp`**

```cpp
#include <atomic>
#include <chrono>
#include <latch>
#include <stdexcept>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <atp/pipeline.hpp>
#include <atp/pipeline_runner.hpp>

#include "pipeline_test_support.hpp"

namespace {

using atp_tests::event_log;
using atp_tests::probe_module;

// Пайплайн: root[a, stage[b, deep[c]], d] — на нём проверяются каскады и раскладка.
struct rig {
    atp::pipeline pipe;
    event_log log;
    probe_module* a;
    probe_module* b;
    probe_module* c;
    probe_module* d;
    atp::group* stage;
    atp::group* deep;

    rig() {
        a = &pipe.root().make<probe_module>("a", log, "a");
        stage = &pipe.root().add_group("stage");
        b = &stage->make<probe_module>("b", log, "b");
        deep = &stage->add_group("deep");
        c = &deep->make<probe_module>("c", log, "c");
        d = &pipe.root().make<probe_module>("d", log, "d");
    }
};

TEST(PipelineRunner, CascadesFollowInsertionOrderAndReverseOnStop) {
    rig r;
    std::latch ticked(1);
    r.a->first_iterate = &ticked;

    atp::pipeline_runner runner(1);
    runner.start(r.pipe);
    ticked.wait();
    runner.stop();

    std::vector<std::string> expected{"a", "b", "c", "d"};
    EXPECT_EQ(r.log.order_of("initialize"), expected);
    EXPECT_EQ(r.log.order_of("start"), expected);
    std::vector<std::string> reversed{"d", "c", "b", "a"};
    EXPECT_EQ(r.log.order_of("stop"), reversed);
}

TEST(PipelineRunner, FailFastRollsBackInitializedModulesOnly) {
    rig r;
    r.c->throw_in = "start";

    atp::pipeline_runner runner(1);
    EXPECT_THROW(runner.start(r.pipe), std::runtime_error);
    EXPECT_FALSE(runner.running());
    // initialize прошли все, start дошёл до c — stop получают прошедшие
    // initialize, в обратном порядке
    std::vector<std::string> reversed{"d", "c", "b", "a"};
    EXPECT_EQ(r.log.order_of("stop"), reversed);
    EXPECT_TRUE(r.log.order_of("iterate").empty());   // потоки не создавались
}

TEST(PipelineRunner, AssignmentsPlaceGroupsOnThreads) {
    rig r;
    std::latch ticked(3);
    r.a->first_iterate = &ticked;   // root → поток 0 (умолчание)
    r.b->first_iterate = &ticked;   // stage → поток 1 (явно)
    r.c->first_iterate = &ticked;   // deep не назначен → inline у stage

    atp::pipeline_runner runner(2);
    runner.assign(*r.stage, 1);
    runner.start(r.pipe);
    ticked.wait();
    runner.stop();

    auto root_thread = r.log.iterate_thread("a");
    auto stage_thread = r.log.iterate_thread("b");
    EXPECT_NE(root_thread, std::thread::id{});
    EXPECT_NE(stage_thread, std::thread::id{});
    EXPECT_NE(root_thread, stage_thread);                       // разные потоки пула
    EXPECT_EQ(r.log.iterate_thread("c"), stage_thread);         // inline наследует поток stage
    EXPECT_EQ(r.log.iterate_thread("d"), root_thread);
}

TEST(PipelineRunner, EmptyConfigurationRunsEverythingOnOneThread) {
    rig r;
    std::latch ticked(1);
    r.c->first_iterate = &ticked;

    atp::pipeline_runner runner(4);                              // назначений нет
    runner.start(r.pipe);
    ticked.wait();
    runner.stop();

    auto t = r.log.iterate_thread("a");
    EXPECT_EQ(r.log.iterate_thread("b"), t);
    EXPECT_EQ(r.log.iterate_thread("c"), t);
    EXPECT_EQ(r.log.iterate_thread("d"), t);
}

TEST(PipelineRunner, IdleThreadBacksOffAndWakesOnData) {
    atp::pipeline pipe;
    std::latch delivered(1);

    struct feed_outputs : atp::io::outputs {
        atp::io::output<int>& value = make<atp::io::output<int>>("value");
    };
    struct drain_inputs : atp::io::inputs {
        atp::io::queued_input<int>& value = make<atp::io::queued_input<int>>("value");
    };
    // Источник молчит до отмашки теста; потребитель считает свои пассы
    // и сигналит о доставке.
    class gated_source : public atp::module<atp::io::inputs, feed_outputs> {
       public:
        std::atomic<bool> go{false};
        atp::work_status iterate(std::stop_token) override {
            if (!go.exchange(false)) {
                return atp::work_status::idle;
            }
            outputs().value(7);
            return atp::work_status::busy;
        }
    };
    class counting_sink : public atp::module<drain_inputs, atp::io::outputs> {
       public:
        std::latch* delivered = nullptr;
        std::atomic<int> passes{0};
        atp::work_status iterate(std::stop_token) override {
            ++passes;
            if (inputs().value.try_pop()) {
                delivered->count_down();
                return atp::work_status::busy;
            }
            return atp::work_status::idle;
        }
    };

    atp::group& left = pipe.root().add_group("left");
    gated_source& src = left.make<gated_source>("src");
    left.expose_output("out", "src.value");
    atp::group& right = pipe.root().add_group("right");
    counting_sink& sink = right.make<counting_sink>("sink");
    sink.delivered = &delivered;
    right.expose_input("in", "sink.value");
    pipe.root().connect("left.out", "right.in");

    atp::pipeline_runner runner(2);
    runner.assign(left, 0);
    runner.assign(right, 1);
    runner.start(pipe);

    // Окно простоя: здесь sleep уместен — тест наблюдает именно темп
    // простаивающего потока, будить его нечем и незачем.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const int idle_passes = sink.passes.load();
    src.go = true;                       // источник оживает на своём потоке
    delivered.wait();                    // спящий потребитель проснулся и получил данные
    runner.stop();

    // Busy-loop дал бы миллионы пассов за 100 мс; backoff — десятки.
    EXPECT_LT(idle_passes, 1000);
}

TEST(PipelineRunner, StopInterruptsIdleSleep) {
    atp::pipeline pipe;
    class idler : public atp::module<atp::io::inputs, atp::io::outputs> {};  // iterate по умолчанию — idle
    pipe.root().make<idler>("sleepy");

    atp::pipeline_runner runner(1);
    runner.start(pipe);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));  // поток дошёл до потолка сна
    const auto begin = std::chrono::steady_clock::now();
    runner.stop();                       // сон прерываем стоп-токеном — join не ждёт интервал
    const auto elapsed = std::chrono::steady_clock::now() - begin;
    // Щедрая граница ловит только настоящее зависание (непрерываемое ожидание),
    // а не шум планировщика.
    EXPECT_LT(elapsed, std::chrono::seconds(1));
}

TEST(PipelineRunner, ValidatesUnsafeCrossThreadConnectionsBeforeCascades) {
    atp::pipeline pipe;
    event_log log;

    struct out_ports : atp::io::outputs {
        atp::io::output<int>& value = make<atp::io::output<int>>("value");
    };
    struct in_ports : atp::io::inputs {
        atp::io::input<int>& value = make<atp::io::input<int>>("value", atp::io::unsafe);
    };
    class producer : public atp::module<atp::io::inputs, out_ports> {};
    class consumer : public atp::module<in_ports, atp::io::outputs> {};

    atp::group& left = pipe.root().add_group("left");
    left.make<producer>("p");
    left.expose_output("out", "p.value");
    atp::group& right = pipe.root().add_group("right");
    right.make<consumer>("c");
    right.expose_input("in", "c.value");
    pipe.root().connect("left.out", "right.in");

    atp::pipeline_runner split(2);
    split.assign(left, 0);
    split.assign(right, 1);
    EXPECT_THROW(split.start(pipe), std::runtime_error);   // unsafe через границу потоков

    atp::pipeline_runner together(1);                       // те же группы на одном потоке — ок
    together.start(pipe);
    together.stop();
}

TEST(PipelineRunner, ConfigurationErrors) {
    rig r;
    atp::pipeline_runner runner(2);
    EXPECT_THROW(atp::pipeline_runner(0), std::invalid_argument);
    EXPECT_THROW(runner.assign(*r.stage, 2), std::out_of_range);

    atp::group stranger("stranger");
    runner.assign(stranger, 0);
    EXPECT_THROW(runner.start(r.pipe), std::invalid_argument);  // назначение вне дерева
}

}  // namespace
```

Примечание: `make<probe_module>("a", log, "a")` — первая строка имя в группе, дальше аргументы конструктора.

`tests/CMakeLists.txt` — добавить `pipeline_runner_tests.cpp`.

- [ ] **Step 3: Не компилируется** (нет `<atp/pipeline_runner.hpp>`).

- [ ] **Step 4: Реализация `src/atp/pipeline_runner.hpp`**

```cpp
#ifndef ANITOOLSPLATFORM_PIPELINE_RUNNER_HPP
#define ANITOOLSPLATFORM_PIPELINE_RUNNER_HPP

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <atp/group.hpp>
#include <atp/pipeline.hpp>

namespace atp {

// Исполнитель пайплайна: пул потоков + жизненный цикл. Владеет только
// потоками и раскладкой; пайплайн — по ссылке на время работы. Все методы
// управления (assign/start/stop/wait/running/error) — только с потока-владельца;
// из потоков пула и кода модулей раннером управлять нельзя. Конкурентный
// stop() во время wait() исключён контрактом, а не синхронизацией.
class pipeline_runner {
   public:
    explicit pipeline_runner(std::size_t threads) : thread_count_(threads) {
        if (threads == 0) {
            throw std::invalid_argument("pipeline_runner: thread pool cannot be empty");
        }
    }

    pipeline_runner(const pipeline_runner&) = delete;
    pipeline_runner& operator=(const pipeline_runner&) = delete;

    ~pipeline_runner() {
        stop();
    }

    // Раскладка «группа → поток» — конфигурация развёртывания, задаётся
    // до старта. Невыделенная группа исполняется inline у ближайшего
    // назначенного предка; корень без назначения — поток 0.
    void assign(const group& g, std::size_t thread_index) {
        if (running_) {
            throw std::logic_error("cannot assign while pipeline is running");
        }
        if (thread_index >= thread_count_) {
            throw std::out_of_range("thread index " + std::to_string(thread_index) + " is outside the pool of " +
                                    std::to_string(thread_count_));
        }
        assigned_[&g] = thread_index;
    }

    void start(pipeline& p) {
        if (running_) {
            throw std::logic_error("pipeline is already running");
        }
        pipeline_ = &p;
        // Шаг 1: чистые проверки конфигурации — до каскадов, откатывать нечего.
        build_thread_map(p.root());
        validate_connections(p.root());
        run_cascades(p);
        launch_threads();
        running_ = true;
    }

    // Идемпотентен и не бросает: ошибки stop-каскада складываются в error_.
    void stop() {
        if (!running_) {
            return;
        }
        stop_source_.request_stop();
        shutdown();
    }

    [[nodiscard]] bool running() const {
        return running_;
    }

   private:
    void build_thread_map(const group& root) {
        thread_of_.clear();
        std::size_t matched = 0;
        map_group(root, 0, matched);
        // Каждое назначение обязано найтись в дереве: молча проигнорированная
        // «чужая» группа означала бы незамеченную ошибку конфигурации.
        if (matched != assigned_.size()) {
            throw std::invalid_argument("assigned group is not part of the pipeline");
        }
    }

    void map_group(const group& g, std::size_t inherited, std::size_t& matched) {
        auto it = assigned_.find(&g);
        std::size_t index = inherited;
        if (it != assigned_.end()) {
            index = it->second;
            ++matched;
        }
        thread_of_[&g] = index;
        for (const group::element& e : g.elements()) {
            if (e.subgroup) {
                map_group(*e.subgroup, index, matched);
            }
        }
    }

    // Критерий — граница потоков, не групп: соседям по потоку safe не нужен.
    void validate_connections(const group& g) const {
        for (const group::connection& c : g.connections()) {
            if (thread_of_.at(c.out_owner) != thread_of_.at(c.in_owner) && !c.in->thread_safe()) {
                throw std::runtime_error("cross-thread connection into unsafe input '" + c.in->name() + "'");
            }
        }
        for (const group::element& e : g.elements()) {
            if (e.subgroup) {
                validate_connections(*e.subgroup);
            }
        }
    }

    void collect_modules(const group& g) {
        for (const group::element& e : g.elements()) {
            if (e.module) {
                modules_.push_back(e.module.get());
            } else {
                collect_modules(*e.subgroup);
            }
        }
    }

    void run_cascades(pipeline& p) {
        modules_.clear();
        collect_modules(p.root());
        std::size_t initialized = 0;
        try {
            for (module_base* m : modules_) {
                m->initialize(p.context());
                ++initialized;
            }
            for (module_base* m : modules_) {
                m->start(p.context());
            }
        } catch (...) {
            // Fail-fast: stop всем, кто прошёл initialize, в обратном порядке.
            // Ошибки отката глотаются — первопричина важнее.
            for (std::size_t i = initialized; i > 0; --i) {
                try {
                    modules_[i - 1]->stop(p.context());
                } catch (...) {  // NOLINT(bugprone-empty-catch)
                }
            }
            reset_state();
            throw;
        }
    }

    void launch_threads() {
        stop_source_ = {};  // свежий источник: раннер мог уже отработать цикл
        std::vector<std::vector<const group*>> per_thread(thread_count_);
        collect_units(pipeline_->root(), per_thread);
        for (std::size_t i = 0; i < thread_count_; ++i) {
            if (per_thread[i].empty()) {
                continue;  // пустому потоку нечего делать — не создаём
            }
            threads_.emplace_back([this, groups = std::move(per_thread[i])] { run_loop(groups); });
        }
    }

    // Единицы исполнения: явно назначенные группы + корень (его умолчание —
    // поток 0). Остальные достижимы inline из своих предков.
    void collect_units(const group& g, std::vector<std::vector<const group*>>& per_thread) const {
        if (&g == &pipeline_->root() || assigned_.contains(&g)) {
            per_thread[thread_of_.at(&g)].push_back(&g);
        }
        for (const group::element& e : g.elements()) {
            if (e.subgroup) {
                collect_units(*e.subgroup, per_thread);
            }
        }
    }

    // Темп простоя: первый простойный пасс — yield (дёшево пережить короткую
    // паузу), дальше сон с удвоением до потолка; busy-пасс сбрасывает темп.
    // Константы фиксированы: десятки мс латентности пробуждения — осознанный
    // контракт платформы, не настройка развёртывания.
    static constexpr auto idle_sleep_initial = std::chrono::milliseconds(1);
    static constexpr auto idle_sleep_cap = std::chrono::milliseconds(10);

    void run_loop(const std::vector<const group*>& groups) {
        std::stop_token token = stop_source_.get_token();
        // Сон прерываем стоп-токеном: request_stop (ошибка или stop()) будит
        // мгновенно, спящий поток не оттягивает остановку.
        std::mutex sleep_mutex;
        std::condition_variable_any sleep_cv;
        std::chrono::milliseconds delay{};  // 0 — ещё не спим, только yield
        try {
            while (!token.stop_requested()) {
                work_status pass = work_status::idle;
                for (const group* g : groups) {
                    if (iterate_group(*g, token) == work_status::busy) {
                        pass = work_status::busy;
                    }
                }
                if (pass == work_status::busy) {
                    delay = {};
                    continue;
                }
                if (delay == std::chrono::milliseconds{}) {
                    std::this_thread::yield();
                    delay = idle_sleep_initial;
                    continue;
                }
                std::unique_lock lock(sleep_mutex);
                sleep_cv.wait_for(lock, token, delay, [] { return false; });
                delay = std::min(delay * 2, idle_sleep_cap);
            }
        } catch (...) {
            capture_error(std::current_exception());
        }
    }

    // Собственные модули группы + inline-поддеревья в позиции своего порядка
    // вставки; назначенные подгруппы пропускаются — у них свой поток.
    // Возврат — статус пасса: busy побеждает, по нему run_loop держит темп.
    work_status iterate_group(const group& g, const std::stop_token& token) {
        work_status pass = work_status::idle;
        for (const group::element& e : g.elements()) {
            if (token.stop_requested()) {
                return pass;
            }
            if (e.module) {
                if (e.module->iterate(token) == work_status::busy) {
                    pass = work_status::busy;
                }
            } else if (!assigned_.contains(e.subgroup.get())) {
                if (iterate_group(*e.subgroup, token) == work_status::busy) {
                    pass = work_status::busy;
                }
            }
        }
        return pass;
    }

    // Первая ошибка побеждает; остановка — общая на весь пайплайн.
    // request_stop под замком: он входит в предикат wait(), изменение
    // условия вне мьютекса — потерянное пробуждение.
    void capture_error(std::exception_ptr e) {
        {
            std::lock_guard lock(error_mutex_);
            if (!error_) {
                error_ = std::move(e);
            }
            stop_source_.request_stop();
        }
        error_cv_.notify_all();
    }

    void store_error(std::exception_ptr e) {
        std::lock_guard lock(error_mutex_);
        if (!error_) {
            error_ = std::move(e);
        }
    }

    // Общий хвост stop()/wait(): дождаться потоков, каскад stop в обратном
    // порядке, сбросить рабочее состояние.
    void shutdown() {
        for (std::jthread& t : threads_) {
            if (t.joinable()) {
                t.join();
            }
        }
        threads_.clear();
        for (auto it = modules_.rbegin(); it != modules_.rend(); ++it) {
            try {
                (*it)->stop(pipeline_->context());
            } catch (...) {
                store_error(std::current_exception());  // stop() не бросает — ошибка доступна через error()
            }
        }
        reset_state();
    }

    void reset_state() {
        modules_.clear();
        thread_of_.clear();
        pipeline_ = nullptr;
        running_ = false;
    }

    std::size_t thread_count_;
    std::unordered_map<const group*, std::size_t> assigned_;
    std::unordered_map<const group*, std::size_t> thread_of_;
    std::vector<module_base*> modules_;  // DFS-порядок вставки — порядок каскадов
    std::vector<std::jthread> threads_;
    std::stop_source stop_source_;
    pipeline* pipeline_ = nullptr;
    bool running_ = false;

    mutable std::mutex error_mutex_;
    std::condition_variable error_cv_;  // будит wait(); условия предиката меняются под error_mutex_
    std::exception_ptr error_;          // первая ошибка исполнения; хранится до следующего start()
};

}  // namespace atp

#endif  // ANITOOLSPLATFORM_PIPELINE_RUNNER_HPP
```

- [ ] **Step 5: Тесты зелёные**

```powershell
cmake --build cmake-build-debug --target atp_tests; cmake-build-debug\tests\atp_tests.exe --gtest_filter='PipelineRunner.*'
```
Ожидание: `[  PASSED  ] 8 tests`. Затем полный `ctest --test-dir cmake-build-debug` — 100%.

- [ ] **Step 6: Предложить пользователю коммит**

Сообщение: `add pipeline_runner: thread pool with group assignments, cascades, validation`

---

### Task 9: `pipeline_runner` — ошибки исполнения, `wait()/error()`, сквозной поток данных

**Files:**
- Modify: `src/atp/pipeline_runner.hpp`
- Test: `tests/pipeline_runner_tests.cpp`

**Interfaces:**
- Consumes: `capture_error/store_error/shutdown` (Task 8).
- Produces:
  - `void wait()` — «работать до аварии»: блокируется до первой ошибки исполнения (у здорового пайплайна — бессрочно), выполняет остановку, **перебрасывает** первопричину; перебрасывает и на уже остановленном пайплайне, пока ошибка хранится (до следующего `start()`)
  - `std::exception_ptr error() const` — nullptr, если ошибок не было

- [ ] **Step 1: Падающие тесты**

```cpp
TEST(PipelineRunner, IterateFailureStopsPipelineAndWaitRethrows) {
    rig r;
    r.b->throw_in = "iterate";

    atp::pipeline_runner runner(2);
    runner.assign(*r.stage, 1);
    runner.start(r.pipe);
    EXPECT_THROW(runner.wait(), std::runtime_error);   // первопричина — из b
    EXPECT_FALSE(runner.running());
    EXPECT_NE(runner.error(), nullptr);
    // каскад stop прошёл всем в обратном порядке, несмотря на ошибку
    std::vector<std::string> reversed{"d", "c", "b", "a"};
    EXPECT_EQ(r.log.order_of("stop"), reversed);
}

TEST(PipelineRunner, FirstErrorWins) {
    rig r;
    r.a->throw_in = "iterate";   // оба бросают; ошибка ровно одна — первая
    r.b->throw_in = "iterate";

    atp::pipeline_runner runner(2);
    runner.assign(*r.stage, 1);
    runner.start(r.pipe);
    EXPECT_THROW(runner.wait(), std::runtime_error);
    EXPECT_NE(runner.error(), nullptr);                // слот заполнен один раз
}

TEST(PipelineRunner, StopIsIdempotentAndErrorIsClean) {
    rig r;
    std::latch ticked(1);
    r.a->first_iterate = &ticked;

    atp::pipeline_runner runner(1);
    runner.start(r.pipe);
    ticked.wait();
    runner.stop();
    runner.stop();                                     // второй вызов — no-op
    EXPECT_EQ(runner.error(), nullptr);
}

TEST(PipelineRunner, WaitAfterStopRethrowsPendingError) {
    rig r;
    std::latch reached(1);
    r.a->first_iterate = &reached;   // зонд сигналит latch до броска (см. probe_module::iterate)
    r.a->throw_in = "iterate";

    atp::pipeline_runner runner(1);
    runner.start(r.pipe);
    reached.wait();
    runner.stop();                                     // не бросает; после join ошибка захвачена
    EXPECT_THROW(runner.wait(), std::runtime_error);   // stop() не съел первопричину
    EXPECT_NE(runner.error(), nullptr);
}

TEST(PipelineRunner, WaitOnIdleRunnerIsNoOp) {
    atp::pipeline_runner runner(1);
    runner.wait();                                     // не стартовал, ошибки нет — сразу возврат
    EXPECT_EQ(runner.error(), nullptr);
}

TEST(PipelineRunner, SecondWaitRethrowsSameError) {
    rig r;
    r.b->throw_in = "iterate";

    atp::pipeline_runner runner(1);
    runner.start(r.pipe);
    EXPECT_THROW(runner.wait(), std::runtime_error);
    EXPECT_THROW(runner.wait(), std::runtime_error);   // ошибка хранится до следующего start()
}

TEST(PipelineRunner, DestructorStopsRunningPipeline) {
    rig r;
    std::latch ticked(1);
    r.a->first_iterate = &ticked;
    {
        atp::pipeline_runner runner(1);
        runner.start(r.pipe);
        ticked.wait();
    }                                                  // ~pipeline_runner → stop()
    std::vector<std::string> reversed{"d", "c", "b", "a"};
    EXPECT_EQ(r.log.order_of("stop"), reversed);
}

TEST(PipelineRunner, DataFlowsBetweenThreadsThroughExposedPorts) {
    atp::pipeline pipe;
    std::latch delivered(1);

    struct out_ports : atp::io::outputs {
        atp::io::output<int>& value = make<atp::io::output<int>>("value");
    };
    struct in_ports : atp::io::inputs {
        atp::io::input<int>& value = make<atp::io::input<int>>("value");   // safe — умолчание
    };
    // Производитель шлёт один раз; потребитель сигналит о доставке.
    class producer : public atp::module<atp::io::inputs, out_ports> {
       public:
        atp::work_status iterate(std::stop_token) override {
            if (sent_) {
                return atp::work_status::idle;
            }
            sent_ = true;
            outputs().value(42);
            return atp::work_status::busy;
        }

       private:
        bool sent_ = false;
    };
    class consumer : public atp::module<in_ports, atp::io::outputs> {
       public:
        std::latch* delivered = nullptr;
        void initialize(atp::module_context&) override {
            watcher_.watch(inputs().value, [this](const int&) { delivered->count_down(); });
        }
        atp::work_status iterate(std::stop_token) override {
            return watcher_.poll();
        }

       private:
        atp::io::watcher watcher_;
    };

    atp::group& left = pipe.root().add_group("left");
    left.make<producer>("p");
    left.expose_output("out", "p.value");
    atp::group& right = pipe.root().add_group("right");
    consumer& c = right.make<consumer>("c");
    c.delivered = &delivered;
    right.expose_input("in", "c.value");
    pipe.root().connect("left.out", "right.in");

    atp::pipeline_runner runner(2);
    runner.assign(left, 0);
    runner.assign(right, 1);
    runner.start(pipe);
    delivered.wait();
    runner.stop();

    EXPECT_EQ(c.inputs().value.get(), 42);
}
```

- [ ] **Step 2: Не компилируется** (нет `wait/error`).

- [ ] **Step 3: Реализация** — в публичную часть `pipeline_runner`:

```cpp
    // Работать до аварии: блокируется до первой ошибки исполнения, затем
    // обычная остановка и переброс первопричины. У здорового пайплайна
    // блокируется бессрочно — остановить его может только владелец, а он
    // здесь. Предикат сразу общий (ошибка ИЛИ запрошен стоп): будущая
    // остановка по инициативе модулей добавит request_stop+notify под тем
    // же замком, wait() не изменится. Переброс — и на уже остановленном
    // пайплайне: порядок «stop(), потом wait()» не глотает первопричину.
    void wait() {
        if (running_) {
            std::stop_token token = stop_source_.get_token();
            {
                std::unique_lock lock(error_mutex_);
                error_cv_.wait(lock, [&] { return error_ != nullptr || token.stop_requested(); });
            }
            shutdown();
        }
        if (std::exception_ptr e = error()) {
            std::rethrow_exception(e);
        }
    }

    [[nodiscard]] std::exception_ptr error() const {
        std::lock_guard lock(error_mutex_);
        return error_;
    }
```

И в `start()` — сброс ошибки прошлого цикла: в начале, после проверки `running_`:

```cpp
        {
            std::lock_guard lock(error_mutex_);
            error_ = nullptr;
        }
```

- [ ] **Step 4: Тесты зелёные** (`PipelineRunner.*`, `[  PASSED  ] 16 tests`), затем полный `ctest` — 100%.

- [ ] **Step 5: Предложить пользователю коммит**

Сообщение: `pipeline_runner: execution errors, wait()/error(), cross-thread data flow`

---

### Task 10: демо-пайплайн + финальная сверка

**Files:**
- Modify: `examples/demo/main.cpp` (переписать на пайплайн)
- Modify: `examples/demo/CMakeLists.txt` (линковать `atp_host`, добавить `${ATP_HOST_HEADERS}` в сорцы)
- Modify: `docs/2026-07-11-pipeline-platform-resume.md` — отметить завершение исполнения плана (спека платформы не сохранилась, канонический источник — этот план)

**Interfaces:**
- Consumes: всё из Tasks 4–9.

- [ ] **Step 1: `examples/demo/CMakeLists.txt`**

```cmake
# Демо-приложение платформы. Заголовки библиотеки указаны в сорцах цели —
# только для видимости в IDE (глобы определены в корневом CMakeLists.txt).
add_executable(atp_demo main.cpp ${ATP_PLATFORM_HEADERS} ${ATP_HOST_HEADERS})
target_link_libraries(atp_demo PRIVATE atp_host)
```

- [ ] **Step 2: `examples/demo/main.cpp` — мини-пайплайн**

```cpp
#include <iostream>
#include <latch>
#include <stop_token>
#include <string>

#include <atp/module.hpp>
#include <atp/pipeline.hpp>
#include <atp/pipeline_runner.hpp>

namespace {

struct source_outputs : atp::io::outputs {
    atp::io::output<int>& number = make<atp::io::output<int>>("number");
};

struct sink_inputs : atp::io::inputs {
    atp::io::input<int>& number = make<atp::io::input<int>>("number");  // safe: границу потоков выбирает раскладка
    atp::io::queued_input<int>& history = make<atp::io::queued_input<int>>("history");
};

// Источник: несколько значений и тишина — демо конечное.
class source_module : public atp::module<atp::io::inputs, source_outputs, "source"> {
   public:
    atp::work_status iterate(std::stop_token) override {
        if (next_ > 3) {
            return atp::work_status::idle;  // всё отправлено — потоку можно спать
        }
        outputs().number(next_++);
        return atp::work_status::busy;
    }

   private:
    int next_ = 1;
};

class sink_module : public atp::module<sink_inputs, atp::io::outputs, "sink"> {
   public:
    std::latch* done = nullptr;

    void initialize(atp::module_context&) override {
        watcher_.watch(inputs().number, [this](const int& value) {
            std::cout << "sink received: " << value << '\n';
            if (value == 3 && done) {
                done->count_down();
            }
        });
    }
    atp::work_status iterate(std::stop_token) override {
        return watcher_.poll();
    }

   private:
    atp::io::watcher watcher_;
};

}  // namespace

int main() {
    atp::pipeline pipe;
    std::latch done(1);

    // Вложенная группа с собственными портами: снаружи видны только алиасы.
    atp::group& producers = pipe.root().add_group("producers");
    producers.make<source_module>();
    producers.expose_output("numbers", "source.number");

    atp::group& consumers = pipe.root().add_group("consumers");
    sink_module& sink = consumers.make<sink_module>();
    sink.done = &done;
    consumers.expose_input("numbers", "sink.number");
    consumers.expose_input("log", "sink.history");

    pipe.root().connect("producers.numbers", "consumers.numbers");
    pipe.root().connect("producers.numbers", "consumers.log");

    // Раскладка развёртывания: producers — поток 0, consumers — поток 1.
    atp::pipeline_runner runner(2);
    runner.assign(producers, 0);
    runner.assign(consumers, 1);
    runner.start(pipe);
    done.wait();
    runner.stop();

    std::cout << "queued history:";
    while (!sink.inputs().history.empty()) {
        std::cout << ' ' << sink.inputs().history.pop();
    }
    std::cout << '\n';
    return 0;
}
```

- [ ] **Step 3: Сборка и запуск демо**

```powershell
cmake --build cmake-build-debug --target atp_demo; cmake-build-debug\examples\demo\atp_demo.exe
```
Ожидание: строки `sink received: 1..3` (порядок значений строгий, чередование с историей — нет) и `queued history: 1 2 3`.

- [ ] **Step 4: Полный прогон всех тестов**

```powershell
cmake --build cmake-build-debug; ctest --test-dir cmake-build-debug
```
Ожидание: 100% passed.

- [ ] **Step 5: Отметить завершение** в `docs/2026-07-11-pipeline-platform-resume.md` и предложить финальный коммит.

Сообщение: `demo: nested groups pipeline on a two-thread pool`

---

## Self-review плана

- **Покрытие спеки:** thread_safe (T1); inputs()/outputs() через базу + `work_status` из iterate + ABI 3 (T2); src/-раскладка и atp_host + переезд module_loader (T3); группа: владение/имена/порядок (T4), экспорт с владельцами и ре-экспорт (T5), connect+replay+записи+авторазрыв (T6); pipeline (T7); раннер: пул/назначения/inline/наследование/корень→0/пустой поток не создаётся/валидация/каскады/fail-fast/backoff простоя с прерываемым сном и побудкой по данным (T8); ошибки iterate/первая побеждает/wait-на-CV «до аварии»/переброс после stop/повторный wait/wait без старта/error/идемпотентный stop/деструктор/межпоточные данные (T9); демо (T10). Контракт stop/wait — по спеке `.superpowers/specs/2026-07-11-runner-stop-wait-design.md`; контракт простоя iterate — по спеке `.superpowers/specs/2026-07-11-iterate-idle-design.md`. Ограничения первой версии кода не требуют.
- **Типы согласованы:** `element{name, module, subgroup}` (T4) используется раннером (T8); `connection{out, in, out_owner, in_owner}` (T6) — валидацией (T8); `probe_module(event_log&, std::string)` (T8) — тестами T8/T9; `make<M>(name, args...)` (T4) — вызовами `make<probe_module>("a", log, "a")`; `work_status` (T2) — возвратами iterate в зондах/демо и агрегацией `iterate_group`/`run_loop` (T8/T10).
- **Плейсхолдеров нет:** каждый шаг с полным кодом/командой.
