# Платформа исполнения (group-композит/pipeline/pipeline_runner) — план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Реализовать платформу исполнения модулей: `atp::group` (модуль-композит: владение детьми, каскады, экспорт портов алиасами), `atp::pipeline` (корень), `atp::pipeline_runner` (именованные потоки с режимами, раскладка, валидация, ошибки). Ревизия 2 — по спекам `.superpowers/specs/2026-07-11-composite-groups-named-threads-design.md`, `...-runner-stop-wait-design.md`, `...-iterate-idle-design.md`, `...-input-pull-model-design.md`, `...-plugin-pinning-design.md`; прежняя версия плана — в git-истории.

**Architecture:** Группа — наследник `module_base`: жизненный цикл — рекурсивные каскады по детям (локальный fail-fast в initialize), `iterate` — агрегация busy/idle с пропуском detached-подгрупп, порты — собственные aliasing-реестры (записи на порты детей). Раннер: именованные потоки (`on_demand`/`throttled`/`spinning`), карта «порт → поток» строится от владеемых портов модулей, валидация safe-входов по границе потоков, каскады через корень, ошибки — «первая побеждает» + CV-ожидание в `wait()`.

**Tech Stack:** C++23 header-only, googletest, CMake ≥ 4.1 + Ninja, MSVC (CLion, профиль `cmake-build-debug`).

## Global Constraints

- Комментарии в коде — **по-русски**, объясняют «почему», не механику. Стиль — `docs/code_style.md`, `.clang-format` (Chromium base, 4 пробела, 120 колонок, обязательные фигурные скобки).
- Нейминг STL-style: snake_case; члены с `value_`; `_base` для type-erased баз; `T`-префикс у шаблонных параметров; gtest-сьюты PascalCase.
- Канонический include — `<atp/...>` везде, и из `include/`, и из `src/`.
- Header guards: `ANITOOLSPLATFORM_<PATH>_HPP` (в `src/` — тот же формат).
- **НЕ коммитить.** Пользователь коммитит сам. В конце каждой задачи — предложить сообщение коммита и остановиться до его решения (или продолжать по его указанию).
- Профиль `cmake-build-debug` сконфигурирован под **MSVC + Ninja**. Сборка/тесты из shell — только в окружении VS, одной командой через vcvars64 (иначе cl.exe не находит стандартные заголовки):

```powershell
cmd /c '"C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 && cmake --build cmake-build-debug --target atp_tests 2>&1 && cmake-build-debug\tests\atp_tests.exe --gtest_filter="Group.*" 2>&1'
```

Команды в задачах ниже показаны без обёртки — исполнять их в этом окружении.

- Новые заголовки в `include/atp/` попадают в IDE-глоб автоматически, но **не** добавляются в umbrella `include/atp/io.hpp` без явного указания. Для `src/` глоб появится в задаче 4.
- `(void)x` вместо `std::ignore`.

---

### Task 1: `io_base::thread_safe()`

Аксессор потокобезопасности экземпляра — нужен раннеру для валидации межпоточных соединений.

**Files:**
- Modify: `include/atp/io/io_base.hpp` (поле `locking_` уже есть)
- Test: `tests/io_tests.cpp` (сьют `IoBase`)

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

- [ ] **Step 2: Убедиться, что не компилируется** (сборка `atp_tests`; ошибка `'thread_safe' ... no member`).

- [ ] **Step 3: Реализация**

В `include/atp/io/io_base.hpp` рядом с `name()/type()`:

```cpp
    // Потокобезопасность — свойство экземпляра (см. safety); раннер по ней
    // валидирует соединения, пересекающие границу потоков.
    [[nodiscard]] bool thread_safe() const noexcept {
        return locking_;
    }
```

- [ ] **Step 4: Тест зелёный** (`--gtest_filter='IoBase.*'`, `[  PASSED  ] 1 test`).

- [ ] **Step 5: Предложить пользователю коммит**

Сообщение: `add io_base::thread_safe() accessor`

---

### Task 2: `module_base::inputs()/outputs()` + `work_status` из `iterate`

Type-erased доступ к io-реестрам через базу — без него группа-композит не реализует порты, а `connect` по именам невозможен. Той же ABI-волной — контракт простоя: `iterate` возвращает `work_status` (busy/idle) для темпа потоков раннера (спека `.superpowers/specs/2026-07-11-iterate-idle-design.md`).

**Files:**
- Modify: `include/atp/module_base.hpp`
- Modify: `include/atp/module.hpp` (`override` к `inputs()/outputs()`, дефолт iterate)
- Modify: `include/atp/plugin.hpp` (дополнить комментарий abi=3)
- Modify: `tests/module_registry_tests.cpp` (`handmade_module`), `tests/module_factory_tests.cpp` (`bare_module`) — новые виртуалы, `iterate` → idle
- Modify: `examples/plugin_demo/counter_modules.hpp` (`iterate` → busy: счётчик работает на каждом вызове)
- Test: `tests/module_tests.cpp`

**Interfaces:**
- Produces: `virtual io::inputs& module_base::inputs() = 0;` + const-вариант; симметрично `outputs()`. `module<...>::inputs()` — ковариантный override, возвращает `TInputs&` как раньше.
- Produces: алиас `atp::work_status` (`= io::work_status`, сам enum уже в `io/threading.hpp` — pull-модель) и `virtual work_status iterate(std::stop_token) = 0`. Дефолт в `module<>` — idle. Возврат намеренно не `[[nodiscard]]`: одиночный тик в plugin_demo-хостах легитимно игнорирует статус.

- [ ] **Step 1: Написать падающие тесты**

В `tests/module_tests.cpp`:

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

(в include-блок файла — `<stop_token>`, если нет).

- [ ] **Step 2: Убедиться, что не компилируется** (ошибка `no member named 'inputs' in 'atp::module_base'`).

- [ ] **Step 3: Реализация**

`include/atp/module_base.hpp` — forward-объявления io-реестров и include контракта простоя:

```cpp
#include <atp/io/threading.hpp>
```

```cpp
namespace atp::io {
class inputs;
class outputs;
}  // namespace atp::io
```

в namespace `atp` перед классом:

```cpp
using work_status = io::work_status;  // сигнатура iterate пишется atp::work_status
```

в класс `module_base` — смена сигнатуры:

```cpp
    virtual work_status iterate(std::stop_token stop_token) = 0;
```

и аксессоры:

```cpp
    // Type-erased доступ к io-реестрам: машинерия соединений (group)
    // работает с модулем через module_ptr и без этих аксессоров не видела
    // бы портов. module<> реализует их ковариантным override — авторам
    // модулей ничего делать не нужно; группа отдаёт свои aliasing-реестры.
    [[nodiscard]] virtual io::inputs& inputs() = 0;
    [[nodiscard]] virtual const io::inputs& inputs() const = 0;
    [[nodiscard]] virtual io::outputs& outputs() = 0;
    [[nodiscard]] virtual const io::outputs& outputs() const = 0;
```

`include/atp/module.hpp` — пометить четыре метода `override` и сменить дефолт iterate:

```cpp
    work_status iterate(std::stop_token) override {
        return work_status::idle;  // no-op-итерация и есть простой
    }
```

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

`include/atp/plugin.hpp` — комментарий abi=3 (число не меняется):

```cpp
// 2: initialize/start/stop принимают module_context&.
// 3: pull-модель входов (io: -when/+take/watcher); create() возвращает
//    module_ptr (пин библиотеки в делетере); module_base отдаёт io-реестры
//    (inputs()/outputs()); iterate возвращает work_status (контракт
//    простоя для исполнителя).
inline constexpr unsigned plugin_abi = 3;
```

`tests/module_registry_tests.cpp` (`handmade_module`) и `tests/module_factory_tests.cpp` (`bare_module`) — в каждый:

```cpp
    atp::work_status iterate(std::stop_token) override {
        return atp::work_status::idle;
    }

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

(старый `void iterate(...) override {}` убрать; если в файле нет `#include <atp/io.hpp>` — добавить). В `examples/plugin_demo/counter_modules.hpp` `iterate` возвращает `atp::work_status::busy` — счётчик работает на каждом вызове. Хосты `host_static.cpp`/`host_dynamic.cpp` не правятся: возврат не `[[nodiscard]]`. В `examples/demo/main.cpp` `sink_module::iterate` меняет сигнатуру: `atp::work_status iterate(std::stop_token) override { return watcher_.poll(); }`.

- [ ] **Step 4: Полный прогон** (сборка всего + `ctest`). Ожидание: 100% passed (тесты ABI-рукопожатия перекомпилируются согласованно).

- [ ] **Step 5: Предложить пользователю коммит**

Сообщение: `module_base: io registries + work_status iterate (ABI 3 wave)`

---

### Task 3: alias-записи в `io_registry`

Невладеющие записи — порты группы-композита: её реестры содержат алиасы на порты детей. Плюс перечисление владеемых портов — материал карты «порт → поток» у раннера.

**Files:**
- Modify: `include/atp/io/io_registry.hpp`
- Test: `tests/io_tests.cpp` (сьют `IoRegistry`)

**Interfaces:**
- Produces: `template <TItem> TItem& io_registry::alias(std::string name, TItem& port)` — невладеющая запись, дубликат имени — `runtime_error`; `[[nodiscard]] std::vector<TBase*> owned() const` — только владеемые порты; `find/get/at/remove/list` единообразны для обоих видов записей; деструктор алиасы не трогает.

- [ ] **Step 1: Написать падающие тесты**

В `tests/io_tests.cpp`:

```cpp
TEST(IoRegistry, AliasSharesForeignPort) {
    atp::io::input<int> real{"real"};
    atp::io::inputs regs;
    regs.alias("mirror", real);
    EXPECT_EQ(regs.find("mirror"), &real);           // тот же объект, не копия
    real(7);
    EXPECT_EQ(regs.get<atp::io::input<int>>("mirror").get(), 7);
}

TEST(IoRegistry, AliasRejectsDuplicateName) {
    atp::io::inputs regs;
    (void)regs.make<atp::io::input<int>>("port");
    atp::io::input<int> foreign{"foreign"};
    EXPECT_THROW(regs.alias("port", foreign), std::runtime_error);
}

TEST(IoRegistry, OwnedSkipsAliases) {
    atp::io::inputs regs;
    auto& own = regs.make<atp::io::input<int>>("own");
    atp::io::input<int> foreign{"foreign"};
    regs.alias("mirror", foreign);
    auto owned = regs.owned();
    ASSERT_EQ(owned.size(), 1u);
    EXPECT_EQ(owned.front(), &own);
    EXPECT_EQ(regs.list().size(), 2u);               // list видит оба вида записей
}

TEST(IoRegistry, DestructionLeavesAliasedPortAlive) {
    atp::io::input<int> foreign{"foreign"};
    {
        atp::io::inputs regs;
        regs.alias("mirror", foreign);
    }                                                // реестр умер — алиас не владел
    foreign(5);
    EXPECT_EQ(foreign.get(), 5);
}
```

- [ ] **Step 2: Убедиться, что не компилируется** (нет `alias`).

- [ ] **Step 3: Реализация `include/atp/io/io_registry.hpp`**

Запись и хранилище (private):

```cpp
    // Запись различает владение: у владеемой owned держит объект, у алиаса
    // owned пуст — реестр публикует чужой порт (группа-композит показывает
    // порты детей). port валиден всегда.
    struct entry {
        std::unique_ptr<TBase> owned;
        TBase* port = nullptr;
    };

    std::string_view kind_;
    std::unordered_map<std::string, entry> registry_;
```

`make` (тело; try_emplace без аргументов — при дубликате ничего не конструируется и `item` остаётся владельцем для текста ошибки):

```cpp
    template <std::derived_from<TBase> TItem>
    TItem& make(std::string name, safety s = safe) {
        auto item = std::make_unique<TItem>(name, s);
        TItem& ref = *item;
        auto [it, inserted] = registry_.try_emplace(std::move(name));
        if (!inserted) {
            throw std::runtime_error("duplicate " + std::string(kind_) + " name '" + ref.name() + "'");
        }
        it->second = {std::move(item), &ref};
        return ref;
    }
```

новый `alias` (после `make`):

```cpp
    // Невладеющая запись: публикация чужого порта под именем этого реестра.
    // Время жизни — контракт вызывающего: алиас живёт не дольше порта
    // (в группе-композите гарантируется структурно — она владеет детьми).
    template <std::derived_from<TBase> TItem>
    TItem& alias(std::string name, TItem& port) {
        auto [it, inserted] = registry_.try_emplace(std::move(name));
        if (!inserted) {
            throw std::runtime_error("duplicate " + std::string(kind_) + " name '" + it->first + "'");
        }
        it->second = {nullptr, &port};
        return port;
    }
```

новый `owned` (после `list`):

```cpp
    // Только владеемые порты — материал карты «порт → поток» у раннера:
    // реестры групп содержат одни алиасы и выпадают из карты сами.
    [[nodiscard]] std::vector<TBase*> owned() const {
        std::vector<TBase*> result;
        for (const auto& [name, e] : registry_) {
            if (e.owned) {
                result.push_back(e.port);
            }
        }
        return result;
    }
```

`find` и `list` — обращение через `entry`:

```cpp
    [[nodiscard]] TBase* find(const std::string& name) const {
        auto it = registry_.find(name);
        return it == registry_.end() ? nullptr : it->second.port;
    }
```

```cpp
    [[nodiscard]] std::vector<const TBase*> list() const {
        std::vector<const TBase*> result;
        result.reserve(registry_.size());
        for (const auto& [name, e] : registry_) {
            result.push_back(e.port);
        }
        return result;
    }
```

Комментарий к `at()` про «const unique_ptr разыменовывается…» заменить на «const-метод отдаёт неконстантную ссылку: запись хранит указатель, константность реестра не распространяется на порты».

- [ ] **Step 4: Тесты зелёные** (`IoRegistry.*`, `[  PASSED  ] 4 tests`; затем полный `ctest` — 100%).

- [ ] **Step 5: Предложить пользователю коммит**

Сообщение: `io_registry: non-owning alias entries + owned() enumeration`

---

### Task 4: цель `atp_host` + переезд `module_loader.hpp` в `src/`

`include/` остаётся SDK автора модулей; хост-машинерия живёт в `src/`.

**Files:**
- Move: `include/atp/module_loader.hpp` → `src/atp/module_loader.hpp` (содержимое без изменений, `git mv`)
- Modify: `CMakeLists.txt` (цель `atp_host`, глоб `src/`)
- Modify: `tests/CMakeLists.txt`, `examples/plugin_demo/CMakeLists.txt`

**Interfaces:**
- Produces: CMake-цель `atp_host` (INTERFACE): include-путь `src/`, линкует `atp_platform`. Последующие задачи кладут заголовки в `src/atp/` и тестируются через `atp_tests`.

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

`examples/plugin_demo/CMakeLists.txt` — только динамический хост:

```cmake
target_link_libraries(atp_host_dynamic PRIVATE atp_host ${CMAKE_DL_LIBS})
```

- [ ] **Step 3: Полная сборка + все тесты** (100% passed; `atp_demo_plugin` собирается без `src/` в include-путях — цель не линкует `atp_host`).

- [ ] **Step 4: Предложить пользователю коммит**

Сообщение: `move module_loader to src/: host machinery leaves the module SDK`

---

### Task 5: `atp::group` — модуль-композит (состав + жизненный цикл)

**Files:**
- Create: `src/atp/group.hpp`
- Create: `tests/pipeline_test_support.hpp` (журнал + модуль-зонд)
- Create: `tests/group_tests.cpp` (+ в `tests/CMakeLists.txt` в сорцы `atp_tests`)

**Interfaces:**
- Consumes: `module_base::inputs()/outputs()`, `work_status` (Task 2), `io_registry::alias` (Task 3 — понадобится в Task 6), `module_ptr`.
- Produces:
  - `class group : public module_base`; `explicit group(std::string name)`; `get_name()` — имя из конструктора (корень/диагностика; имена детей — в записях родителя)
  - `struct child { std::string name; module_ptr module; bool detached = false; }`; `const std::vector<child>& children() const` — порядок вставки
  - `module_base& add(std::string name, module_ptr)` — `invalid_argument` (null/пустое имя), `runtime_error` (дубликат)
  - `template <M, TArgs...> M& make(std::string name, TArgs&&...)` и `make<TArgs...>()`-вариант с именем из `M::module_name` (`has_module_name`)
  - `group& add_group(std::string name)`; `module_base* find_module(name)`; `group* find_group(name)` — dynamic_cast результата
  - `void set_detached(const group& child, bool)` — служебный крючок раннера (подгруппа исполняется своим потоком); неизвестный ребёнок — `invalid_argument`
  - жизненный цикл: `initialize` — каскад с локальным откатом; `start` — каскад без отката; `stop` — обратный порядок, продолжает при ошибке, первую перебрасывает; `iterate` — агрегация busy/idle, detached пропускаются
  - `inputs()/outputs()` — собственные реестры (в этой задаче пустые; наполняются экспортом в Task 6)

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

- [ ] **Step 2: Падающие тесты `tests/group_tests.cpp`**

```cpp
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <atp/group.hpp>
#include <atp/module.hpp>

#include "pipeline_test_support.hpp"

namespace {

using atp_tests::event_log;
using atp_tests::probe_module;

class named_module : public atp::module<atp::io::inputs, atp::io::outputs, "named"> {};
class plain_module : public atp::module<atp::io::inputs, atp::io::outputs> {};

// Дерево root[a, stage[b, deep[c]], d] — общая фикстура каскадов.
struct rig {
    atp::group root{"root"};
    event_log log;
    probe_module* a;
    probe_module* b;
    probe_module* c;
    probe_module* d;
    atp::group* stage;
    atp::group* deep;
    atp::service_directory services;
    atp::module_context ctx{services};

    rig() {
        a = &root.make<probe_module>("a", log, "a");
        stage = &root.add_group("stage");
        b = &stage->make<probe_module>("b", log, "b");
        deep = &stage->add_group("deep");
        c = &deep->make<probe_module>("c", log, "c");
        d = &root.make<probe_module>("d", log, "d");
    }
};

TEST(Group, OwnsChildrenInInsertionOrder) {
    atp::group g("root");
    g.make<named_module>();                       // имя из типа
    atp::group& sub = g.add_group("sub");
    g.make<plain_module>("tail");                 // имя в точке регистрации

    ASSERT_EQ(g.children().size(), 3u);
    EXPECT_EQ(g.children()[0].name, "named");
    EXPECT_EQ(g.children()[1].module.get(), &sub);  // подгруппа — такой же ребёнок-модуль
    EXPECT_EQ(g.children()[2].name, "tail");
    EXPECT_EQ(g.find_group("sub"), &sub);
    EXPECT_EQ(g.find_group("tail"), nullptr);       // это модуль, не группа
    EXPECT_EQ(sub.get_name(), "sub");
}

TEST(Group, AddAcceptsPrebuiltModule) {
    atp::group g("root");
    atp::module_base& m = g.add("ready", atp::module_ptr{new plain_module});  // в т.ч. из module_registry::create()
    EXPECT_EQ(g.find_module("ready"), &m);
    EXPECT_EQ(g.find_module("missing"), nullptr);
}

TEST(Group, RejectsDuplicateAndEmptyNames) {
    atp::group g("root");
    g.make<plain_module>("one");
    EXPECT_THROW(g.make<plain_module>("one"), std::runtime_error);
    EXPECT_THROW(g.add_group("one"), std::runtime_error);   // общее пространство имён
    EXPECT_THROW(g.make<plain_module>(""), std::invalid_argument);
    EXPECT_THROW(g.add("null", atp::module_ptr{}), std::invalid_argument);
}

TEST(Group, CascadesFollowInsertionOrderAndReverseOnStop) {
    rig r;
    r.root.initialize(r.ctx);
    r.root.start(r.ctx);
    (void)r.root.iterate(std::stop_token{});
    r.root.stop(r.ctx);

    std::vector<std::string> expected{"a", "b", "c", "d"};
    EXPECT_EQ(r.log.order_of("initialize"), expected);
    EXPECT_EQ(r.log.order_of("start"), expected);
    EXPECT_EQ(r.log.order_of("iterate"), expected);
    std::vector<std::string> reversed{"d", "c", "b", "a"};
    EXPECT_EQ(r.log.order_of("stop"), reversed);
}

TEST(Group, InitializeFailureRollsBackLocally) {
    rig r;
    r.c->throw_in = "initialize";
    EXPECT_THROW(r.root.initialize(r.ctx), std::runtime_error);
    // stop получают прошедшие initialize (a, b), в обратном порядке; d не трогался
    std::vector<std::string> rolled{"b", "a"};
    EXPECT_EQ(r.log.order_of("stop"), rolled);
}

TEST(Group, StopContinuesAfterErrorAndRethrowsFirst) {
    rig r;
    r.root.initialize(r.ctx);
    r.b->throw_in = "stop";
    EXPECT_THROW(r.root.stop(r.ctx), std::runtime_error);
    // несмотря на бросок b, stop получили все — обратный порядок сохранён
    std::vector<std::string> reversed{"d", "c", "b", "a"};
    EXPECT_EQ(r.log.order_of("stop"), reversed);
}

TEST(Group, IterateSkipsDetachedAndAggregatesStatus) {
    rig r;
    r.root.set_detached(*r.stage, true);
    EXPECT_EQ(r.root.iterate(std::stop_token{}), atp::work_status::busy);  // зонды busy
    std::vector<std::string> without_stage{"a", "d"};
    EXPECT_EQ(r.log.order_of("iterate"), without_stage);

    atp::group idle_group("idle");
    idle_group.make<plain_module>("silent");                               // дефолтный iterate — idle
    EXPECT_EQ(idle_group.iterate(std::stop_token{}), atp::work_status::idle);
}

TEST(Group, SetDetachedUnknownChildThrows) {
    atp::group g("root");
    atp::group stranger("stranger");
    EXPECT_THROW(g.set_detached(stranger, true), std::invalid_argument);
}

}  // namespace
```

`tests/CMakeLists.txt` — добавить `group_tests.cpp` в сорцы `atp_tests`.

- [ ] **Step 3: Убедиться, что не компилируется** (нет `<atp/group.hpp>`).

- [ ] **Step 4: Реализация `src/atp/group.hpp`**

```cpp
#ifndef ANITOOLSPLATFORM_GROUP_HPP
#define ANITOOLSPLATFORM_GROUP_HPP

#include <concepts>
#include <exception>
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <atp/io.hpp>
#include <atp/module_base.hpp>
#include <atp/module_registry.hpp>

namespace atp {

// Группа — модуль-композит: владеет детьми (модулями и подгруппами — те же
// модули), реализует жизненный цикл каскадами по ним и публикует избранные
// порты детей алиасами в собственных реестрах (см. expose_*, Task 6).
// НЕ единица исполнения: как группа исполняется (свой поток или inline у
// предка), решает раскладка раннера — вложенность здесь только инкапсуляция.
// НЕ потокобезопасна: состав и экспорт — фаза настройки. Каскады зовёт
// раннер; ручной root.initialize() в обход раннера — двойные каскады.
class group : public module_base {
   public:
    // Запись ребёнка: имя в области видимости группы (алиасы, анонимы) +
    // владение (module_ptr несёт пин DLL — модуль плагина держит свою
    // библиотеку). detached — ребёнок исполняется собственным потоком,
    // родительский iterate его пропускает; ставит и снимает раннер.
    struct child {
        std::string name;
        module_ptr module;
        bool detached = false;
    };

    explicit group(std::string name) : name_(std::move(name)) {}

    group(const group&) = delete;
    group& operator=(const group&) = delete;

    // Имя корня и диагностика; имена детей живут в записях родителя.
    [[nodiscard]] std::string_view get_name() const noexcept override {
        return name_;
    }

    // Приём готового модуля — в т.ч. созданного module_registry::create().
    module_base& add(std::string name, module_ptr module) {
        if (!module) {
            throw std::invalid_argument("null module for '" + name + "' in group '" + name_ + "'");
        }
        ensure_unique(name);
        children_.push_back({std::move(name), std::move(module), false});
        return *children_.back().module;
    }

    // Сахар: создать модуль на месте. Аргументы — конструктору модуля.
    template <std::derived_from<module_base> M, typename... TArgs>
        requires std::constructible_from<M, TArgs...>
    M& make(std::string name, TArgs&&... args) {
        module_ptr module(new M(std::forward<TArgs>(args)...), {});  // монолит: пин пуст
        M& ref = static_cast<M&>(*module);
        add(std::move(name), std::move(module));
        return ref;
    }

    // Имя из самого модуля — тот же контракт has_module_name, что в реестре
    // фабрик. При коллизии перегрузок — указывать имя явно.
    template <std::derived_from<module_base> M, typename... TArgs>
        requires has_module_name<M> && std::constructible_from<M, TArgs...>
    M& make(TArgs&&... args) {
        return make<M>(std::string{M::module_name}, std::forward<TArgs>(args)...);
    }

    // Подгруппа — такой же ребёнок-модуль; имя дублируется в её конструктор
    // для get_name (диагностика).
    group& add_group(std::string name) {
        std::string ctor_name = name;
        return make<group>(std::move(name), std::move(ctor_name));
    }

    [[nodiscard]] const std::vector<child>& children() const {
        return children_;
    }

    [[nodiscard]] module_base* find_module(const std::string& name) const {
        const child* c = find_child(name);
        return c ? c->module.get() : nullptr;
    }

    [[nodiscard]] group* find_group(const std::string& name) const {
        return dynamic_cast<group*>(find_module(name));
    }

    // Служебный крючок раннера: подгруппа, назначенная своему потоку,
    // исключается из iterate родителя. Вне раннера не звать.
    void set_detached(const group& detached_child, bool value) {
        for (child& c : children_) {
            if (c.module.get() == &detached_child) {
                c.detached = value;
                return;
            }
        }
        throw std::invalid_argument("group '" + name_ + "' has no such child group");
    }

    // Порты группы — алиасы на порты детей (наполняются expose_*, Task 6).
    [[nodiscard]] io::inputs& inputs() override {
        return inputs_;
    }
    [[nodiscard]] const io::inputs& inputs() const override {
        return inputs_;
    }
    [[nodiscard]] io::outputs& outputs() override {
        return outputs_;
    }
    [[nodiscard]] const io::outputs& outputs() const override {
        return outputs_;
    }

    // --- Жизненный цикл: каскады по порядку вставки ---

    // Локальный fail-fast: бросок ребёнка — stop уже инициализированным в
    // обратном порядке (ошибки отката глотаются — первопричина важнее),
    // затем переброс. Внешние группы откатывают своих предыдущих детей той
    // же логикой — рекурсивно.
    void initialize(module_context& context) override {
        std::size_t done = 0;
        try {
            for (child& c : children_) {
                c.module->initialize(context);
                ++done;
            }
        } catch (...) {
            for (std::size_t i = done; i > 0; --i) {
                try {
                    children_[i - 1].module->stop(context);
                } catch (...) {  // NOLINT(bugprone-empty-catch)
                }
            }
            throw;
        }
    }

    // Отката здесь нет: при ошибке раннер зовёт root.stop() — stop обязан
    // быть корректен после initialize без start (контракт module_base).
    void start(module_context& context) override {
        for (child& c : children_) {
            c.module->start(context);
        }
    }

    // Обратный порядок; при ошибке ребёнка — продолжить остальных, первую
    // ошибку перебросить в конце: остановка важнее её диагностики.
    void stop(module_context& context) override {
        std::exception_ptr first;
        for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
            try {
                it->module->stop(context);
            } catch (...) {
                if (!first) {
                    first = std::current_exception();
                }
            }
        }
        if (first) {
            std::rethrow_exception(first);
        }
    }

    // Агрегация пасса: busy побеждает; detached-дети — у них свой поток.
    work_status iterate(std::stop_token token) override {
        work_status pass = work_status::idle;
        for (child& c : children_) {
            if (token.stop_requested()) {
                return pass;
            }
            if (c.detached) {
                continue;
            }
            if (c.module->iterate(token) == work_status::busy) {
                pass = work_status::busy;
            }
        }
        return pass;
    }

   private:
    [[nodiscard]] const child* find_child(const std::string& name) const {
        // линейный поиск: фаза настройки, составы групп невелики
        for (const child& c : children_) {
            if (c.name == name) {
                return &c;
            }
        }
        return nullptr;
    }

    void ensure_unique(const std::string& name) const {
        if (name.empty()) {
            throw std::invalid_argument("empty child name in group '" + name_ + "'");
        }
        if (find_child(name)) {
            throw std::runtime_error("duplicate name '" + name + "' in group '" + name_ + "'");
        }
    }

    std::string name_;
    std::vector<child> children_;
    io::inputs inputs_;
    io::outputs outputs_;
};

}  // namespace atp

#endif  // ANITOOLSPLATFORM_GROUP_HPP
```

- [ ] **Step 5: Тесты зелёные** (`Group.*`, `[  PASSED  ] 8 tests`; полный `ctest` — 100%).

- [ ] **Step 6: Предложить пользователю коммит**

Сообщение: `add atp::group: composite module (children, cascades, busy/idle aggregation)`

---

### Task 6: `group` — экспорт портов алиасами

**Files:**
- Modify: `src/atp/group.hpp`
- Test: `tests/group_tests.cpp`

**Interfaces:**
- Consumes: `io_registry::alias` (Task 3), `module_base::inputs()/outputs()` (Task 2), состав группы (Task 5).
- Produces:
  - `void expose_input(std::string alias, const std::string& path)` / `void expose_output(...)` — только путь `"<дитя>.<порт>"`; алиас появляется в собственных `inputs()`/`outputs()` группы; ре-экспорт указывает сразу на реальный порт
  - приватные `io::input_base& resolve_input(const std::string& path)` / `io::output_base& resolve_output(...)` — Task 7 строит на них `connect`
  - ошибки: путь без точки — `invalid_argument`; нет ребёнка/порта — `runtime_error`; дубликат алиаса — `runtime_error` (из реестра)

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

TEST(Group, ExposesChildPortsAsOwnAliases) {
    atp::group g("root");
    sink_module& sink = g.make<sink_module>("sink");
    source_module& src = g.make<source_module>("src");
    g.expose_input("in", "sink.number");
    g.expose_output("out", "src.number");

    EXPECT_EQ(&g.inputs().at("in"), &sink.inputs().number);    // тот же объект
    EXPECT_EQ(&g.outputs().at("out"), &src.outputs().number);
    EXPECT_TRUE(g.inputs().owned().empty());                   // реестры группы — только алиасы
}

TEST(Group, PortsVisibleThroughModuleBase) {
    atp::group g("stage");
    g.make<sink_module>("sink");
    g.expose_input("in", "sink.number");
    atp::module_base& as_module = g;
    // Композит: снаружи группа выглядит обычным модулем с портами.
    EXPECT_NE(as_module.inputs().find("in"), nullptr);
}

TEST(Group, ReexportResolvesToRealPortImmediately) {
    atp::group root("root");
    atp::group& inner = root.add_group("inner");
    sink_module& sink = inner.make<sink_module>("sink");
    inner.expose_input("in", "sink.number");
    root.expose_input("outer_in", "inner.in");                 // ре-экспорт алиаса

    EXPECT_EQ(&root.inputs().at("outer_in"), &sink.inputs().number);
}

TEST(Group, ExposeErrors) {
    atp::group g("root");
    g.make<sink_module>("sink");
    g.expose_input("in", "sink.number");
    EXPECT_THROW(g.expose_input("in", "sink.number"), std::runtime_error);      // дубликат алиаса
    EXPECT_THROW(g.expose_input("x", "nobody.number"), std::runtime_error);     // нет такого дитя
    EXPECT_THROW(g.expose_input("y", "sink.missing"), std::runtime_error);      // нет такого порта
    EXPECT_THROW(g.expose_input("z", "sink"), std::invalid_argument);           // путь без точки
    EXPECT_THROW((void)g.inputs().at("missing"), std::runtime_error);
}
```

- [ ] **Step 2: Не компилируется / падает** (нет `expose_input`).

- [ ] **Step 3: Реализация**

Публичное (после `set_detached`):

```cpp
    // --- Экспорт портов: алиасы в собственных реестрах группы ---
    // Только путь «<дитя>.<порт>»: порт находится через модульный интерфейс
    // ребёнка — модуль и подгруппа неразличимы, ре-экспорт указывает сразу
    // на реальный порт. Перегрузки по ссылке нет намеренно: принадлежность
    // порта группе непроверяема, ошибка вызывающего молча искажала бы
    // валидацию межпоточности.

    void expose_input(std::string alias, const std::string& path) {
        inputs_.alias(std::move(alias), resolve_input(path));
    }

    void expose_output(std::string alias, const std::string& path) {
        outputs_.alias(std::move(alias), resolve_output(path));
    }
```

Приватное (после `ensure_unique`):

```cpp
    // Путь «<дитя>.<порт>» в области видимости группы.
    [[nodiscard]] std::pair<module_base*, std::string> split_path(const std::string& path) const {
        auto dot = path.find('.');
        if (dot == std::string::npos || dot == 0 || dot + 1 == path.size()) {
            throw std::invalid_argument("path '" + path + "' in group '" + name_ + "': expected '<child>.<port>'");
        }
        module_base* child_module = find_module(path.substr(0, dot));
        if (!child_module) {
            throw std::runtime_error("group '" + name_ + "' has no child '" + path.substr(0, dot) + "'");
        }
        return {child_module, path.substr(dot + 1)};
    }

    [[nodiscard]] io::input_base& resolve_input(const std::string& path) const {
        auto [child_module, port_name] = split_path(path);
        io::input_base* port = child_module->inputs().find(port_name);
        if (!port) {
            throw std::runtime_error("child '" + std::string(child_module->get_name()) + "' has no input '" +
                                     port_name + "' (path '" + path + "' in group '" + name_ + "')");
        }
        return *port;
    }

    [[nodiscard]] io::output_base& resolve_output(const std::string& path) const {
        auto [child_module, port_name] = split_path(path);
        io::output_base* port = child_module->outputs().find(port_name);
        if (!port) {
            throw std::runtime_error("child '" + std::string(child_module->get_name()) + "' has no output '" +
                                     port_name + "' (path '" + path + "' in group '" + name_ + "')");
        }
        return *port;
    }
```

Include-блок дополнить `<utility>` (std::pair), если нет.

- [ ] **Step 4: Тесты зелёные** (`Group.*`, `[  PASSED  ] 12 tests`).

- [ ] **Step 5: Предложить пользователю коммит**

Сообщение: `group: expose child ports as aliases in its own registries`

---

### Task 7: `group` — соединения по путям + авторазрыв

**Files:**
- Modify: `src/atp/group.hpp`
- Test: `tests/group_tests.cpp`

**Interfaces:**
- Consumes: `resolve_input/resolve_output` (Task 6), `output_base::connect/disconnect` (io-слой).
- Produces:
  - `void connect(const std::string& from, const std::string& to)` / `..., io::replay_t)`
  - `struct connection { io::output_base* out; io::input_base* in; }` — владельцев в записях нет (карту «порт → поток» раннер строит сам)
  - `const std::vector<connection>& connections() const` — записи ЭТОЙ группы
  - `~group()` рвёт эти соединения до разрушения детей

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
    EXPECT_EQ(root.connections()[0].out, &src.outputs().number);
    EXPECT_EQ(root.connections()[0].in, &sink.inputs().number);   // реальный вход, не алиас

    src.outputs().number(7);
    EXPECT_EQ(sink.inputs().number.get(), 7);                     // прямая доставка, без хопов
}

TEST(Group, ConnectReplayDeliversCache) {
    atp::group g("root");
    source_module& src = g.make<source_module>("src");
    sink_module& sink = g.make<sink_module>("sink");
    src.outputs().number(42);                                     // кэш до подключения
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
    {
        atp::group g("root");
        sink_module& sink = g.make<sink_module>("sink");
        src.outputs().number.connect(sink.inputs().number);   // прямое, мимо группы — рвёт вызывающий
        src.outputs().number.disconnect(sink.inputs().number);

        source_module& inner_src = g.make<source_module>("inner_src");
        g.connect("inner_src.number", "sink.number");          // запись группы
        EXPECT_EQ(inner_src.outputs().number.connections(), 1u);
    }                                                      // деструктор рвёт свою запись до детей
    EXPECT_EQ(src.outputs().number.connections(), 0u);
}
```

- [ ] **Step 2: Не компилируется** (нет `connect`).

- [ ] **Step 3: Реализация**

Публичное:

```cpp
    // --- Соединения: пути в области видимости группы ---
    // Запись — только пара портов: владельцев здесь нет, карту
    // «порт → поток» для валидации раннер строит сам от владеемых
    // портов модулей.
    struct connection {
        io::output_base* out;
        io::input_base* in;
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
    // каждой записи ещё живы — «disconnect до разрушения входа» соблюдается
    // конструктивно.
    ~group() override {
        for (const connection& c : connections_) {
            (void)c.out->disconnect(*c.in);
        }
    }
```

Приватное:

```cpp
    void link(const std::string& from, const std::string& to, bool deliver_cached) {
        io::output_base& out = resolve_output(from);
        io::input_base& in = resolve_input(to);
        if (deliver_cached) {
            out.connect(in, io::replay);
        } else {
            out.connect(in);
        }
        connections_.push_back({&out, &in});
    }

    std::vector<connection> connections_;
```

- [ ] **Step 4: Тесты зелёные** (`Group.*`, `[  PASSED  ] 16 tests`).

- [ ] **Step 5: Предложить пользователю коммит**

Сообщение: `group: path-based connect with connection records and auto-disconnect`

---

### Task 8: `atp::pipeline`

**Files:**
- Create: `src/atp/pipeline.hpp`
- Test: `tests/pipeline_tests.cpp` (+ в `tests/CMakeLists.txt`)

**Interfaces:**
- Produces: `pipeline()` (default); `group& root()` (+const); `service_directory& services()`; `module_context& context()`. Корневая группа называется `"root"`. Композит pipeline не требует — это агрегат.

- [ ] **Step 1: Падающий тест**

`tests/pipeline_tests.cpp`:

```cpp
#include <gtest/gtest.h>

#include <atp/pipeline.hpp>

TEST(Pipeline, RootServicesAndContextAreWired) {
    atp::pipeline p;
    EXPECT_EQ(p.root().get_name(), "root");
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

### Task 9: `pipeline_runner` — именованные потоки, раскладка, валидация, каскады, режимы

Самая большая задача: раннер целиком, кроме путей ошибок исполнения (Task 10).

**Files:**
- Create: `src/atp/thread_name.hpp` (платформенное имя потока)
- Create: `src/atp/pipeline_runner.hpp`
- Create: `tests/pipeline_runner_tests.cpp` (+ в `tests/CMakeLists.txt`)

**Interfaces:**
- Consumes: `group::children()/connections()/set_detached()/iterate()` (Tasks 5–7), `pipeline` (Task 8), `io_base::thread_safe()` (Task 1), `io_registry::owned()` (Task 3).
- Produces:
  - `enum class thread_mode { on_demand, throttled, spinning };`
  - `struct thread_options { thread_mode mode = thread_mode::on_demand; std::chrono::milliseconds period{}; };`
  - `pipeline_runner()` (default); `void add_thread(std::string name, thread_options = {})` — до старта; дубликат имени — `runtime_error`; `period` обязателен для throttled и запрещён для остальных — `invalid_argument`
  - `void assign(const group&, const std::string& thread_name)` — неизвестное имя — `invalid_argument` **сразу**; после старта — `logic_error`
  - `void start(pipeline&)`: карта потоков → карта портов → валидация → detach → каскады через root → запуск циклов; повторный старт — `logic_error`
  - `void stop()` — идемпотентен, не бросает; `bool running() const`
  - (Task 10 добавит `wait()`, `error()`)
- Семантика: без единого `add_thread` — неявный поток `"main"` (on_demand); корень без назначения — на первый объявленный поток; невыделенная группа — inline у ближайшего назначенного предка; поток без групп не создаётся; `on_demand` — yield-пасс, затем сон с удвоением `idle_sleep_initial=1ms → idle_sleep_cap=10ms` (параметры режима — умолчания прежние), busy сбрасывает; `throttled` — пасс → `wait_until(next)`, скольжение `next = now + period`; `spinning` — чистый yield-цикл; все сны прерываемы стоп-токеном; имя потока уходит в ОС; ошибки валидации называют потоки по именам.

- [ ] **Step 1: `src/atp/thread_name.hpp`**

```cpp
#ifndef ANITOOLSPLATFORM_THREAD_NAME_HPP
#define ANITOOLSPLATFORM_THREAD_NAME_HPP

#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace atp::detail {

// Имя текущего потока для отладчика/профайлера. Вторая точка платформенных
// веток после module_loader. Ошибки глотаются: имя — диагностика, не логика.
#if defined(_WIN32)
inline void set_current_thread_name(const std::string& name) noexcept {
    // побайтовое расширение: имена потоков ожидаются ASCII — не-ASCII
    // исказит подпись в отладчике, на логику не влияет
    std::wstring wide(name.begin(), name.end());
    (void)::SetThreadDescription(::GetCurrentThread(), wide.c_str());
}
#else
inline void set_current_thread_name(const std::string& name) noexcept {
    (void)::pthread_setname_np(pthread_self(), name.substr(0, 15).c_str());
}
#endif

}  // namespace atp::detail

#endif  // ANITOOLSPLATFORM_THREAD_NAME_HPP
```

- [ ] **Step 2: Падающие тесты `tests/pipeline_runner_tests.cpp`**

```cpp
#include <atomic>
#include <chrono>
#include <latch>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <atp/pipeline.hpp>
#include <atp/pipeline_runner.hpp>

#include "pipeline_test_support.hpp"

namespace {

using atp_tests::event_log;
using atp_tests::probe_module;

// Пайплайн: root[a, stage[b, deep[c]], d] — каскады и раскладка.
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

TEST(PipelineRunner, CascadesRunThroughRunnerAndReverseOnStop) {
    rig r;
    std::latch ticked(1);
    r.a->first_iterate = &ticked;

    atp::pipeline_runner runner;
    runner.start(r.pipe);
    ticked.wait();
    runner.stop();

    std::vector<std::string> expected{"a", "b", "c", "d"};
    EXPECT_EQ(r.log.order_of("initialize"), expected);
    EXPECT_EQ(r.log.order_of("start"), expected);
    std::vector<std::string> reversed{"d", "c", "b", "a"};
    EXPECT_EQ(r.log.order_of("stop"), reversed);
}

TEST(PipelineRunner, StartFailureStopsInitializedInReverse) {
    rig r;
    r.c->throw_in = "start";

    atp::pipeline_runner runner;
    EXPECT_THROW(runner.start(r.pipe), std::runtime_error);
    EXPECT_FALSE(runner.running());
    // initialize прошли все, start дошёл до c — root.stop() получают все,
    // в обратном порядке (stop корректен после initialize без start)
    std::vector<std::string> reversed{"d", "c", "b", "a"};
    EXPECT_EQ(r.log.order_of("stop"), reversed);
    EXPECT_TRUE(r.log.order_of("iterate").empty());   // потоки не создавались
}

TEST(PipelineRunner, AssignmentsPlaceGroupsOnNamedThreads) {
    rig r;
    std::latch ticked(3);
    r.a->first_iterate = &ticked;   // root → первый объявленный поток
    r.b->first_iterate = &ticked;   // stage → "aux" (явно)
    r.c->first_iterate = &ticked;   // deep не назначен → inline у stage

    atp::pipeline_runner runner;
    runner.add_thread("main");
    runner.add_thread("aux");
    runner.assign(*r.stage, "aux");
    runner.start(r.pipe);
    ticked.wait();
    runner.stop();

    auto root_thread = r.log.iterate_thread("a");
    auto stage_thread = r.log.iterate_thread("b");
    EXPECT_NE(root_thread, std::thread::id{});
    EXPECT_NE(stage_thread, std::thread::id{});
    EXPECT_NE(root_thread, stage_thread);                       // разные потоки
    EXPECT_EQ(r.log.iterate_thread("c"), stage_thread);         // inline наследует поток stage
    EXPECT_EQ(r.log.iterate_thread("d"), root_thread);
}

TEST(PipelineRunner, EmptyConfigurationRunsEverythingOnImplicitMain) {
    rig r;
    std::latch ticked(1);
    r.c->first_iterate = &ticked;

    atp::pipeline_runner runner;                                 // ни одного add_thread
    runner.start(r.pipe);
    ticked.wait();
    runner.stop();

    auto t = r.log.iterate_thread("a");
    EXPECT_EQ(r.log.iterate_thread("b"), t);
    EXPECT_EQ(r.log.iterate_thread("c"), t);
    EXPECT_EQ(r.log.iterate_thread("d"), t);
}

TEST(PipelineRunner, ValidatesUnsafeCrossThreadConnectionsWithThreadNames) {
    atp::pipeline pipe;

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

    atp::pipeline_runner split;
    split.add_thread("producing");
    split.add_thread("consuming");
    split.assign(left, "producing");
    split.assign(right, "consuming");
    try {
        split.start(pipe);
        FAIL() << "expected std::runtime_error";
    } catch (const std::runtime_error& error) {
        const std::string what = error.what();                   // имена потоков — в диагностике
        EXPECT_NE(what.find("producing"), std::string::npos);
        EXPECT_NE(what.find("consuming"), std::string::npos);
    }

    atp::pipeline_runner together;                               // те же группы на одном потоке — ок
    together.start(pipe);
    together.stop();
}

TEST(PipelineRunner, ConfigurationErrors) {
    rig r;
    atp::pipeline_runner runner;
    runner.add_thread("main");
    EXPECT_THROW(runner.add_thread("main"), std::runtime_error);                       // дубликат имени
    EXPECT_THROW(runner.add_thread("t", {atp::thread_mode::throttled, {}}), std::invalid_argument);  // период обязателен
    EXPECT_THROW(runner.add_thread("s", {atp::thread_mode::on_demand, std::chrono::milliseconds(5)}),
                 std::invalid_argument);                                               // период запрещён
    EXPECT_THROW(runner.assign(*r.stage, "nowhere"), std::invalid_argument);           // неизвестное имя — сразу

    atp::group stranger("stranger");
    runner.assign(stranger, "main");
    EXPECT_THROW(runner.start(r.pipe), std::invalid_argument);                         // назначение вне дерева
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
    // Источник молчит до отмашки теста; потребитель считает пассы.
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

    atp::pipeline_runner runner;
    runner.add_thread("producing");
    runner.add_thread("consuming");
    runner.assign(left, "producing");
    runner.assign(right, "consuming");
    runner.start(pipe);

    // Окно простоя: sleep уместен — тест наблюдает темп простаивающего
    // потока, будить его нечем и незачем.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const int idle_passes = sink.passes.load();
    src.go = true;
    delivered.wait();
    runner.stop();

    // Busy-loop дал бы миллионы пассов за 100 мс; backoff — десятки.
    EXPECT_LT(idle_passes, 1000);
}

TEST(PipelineRunner, ThrottledPacesIterations) {
    atp::pipeline pipe;
    class counting_module : public atp::module<atp::io::inputs, atp::io::outputs> {
       public:
        std::atomic<int> passes{0};
        atp::work_status iterate(std::stop_token) override {
            ++passes;
            return atp::work_status::busy;  // busy не разгоняет throttled — темп задаёт период
        }
    };
    atp::group& g = pipe.root().add_group("paced");
    counting_module& m = g.make<counting_module>("m");

    atp::pipeline_runner runner;
    runner.add_thread("paced", {atp::thread_mode::throttled, std::chrono::milliseconds(20)});
    runner.assign(g, "paced");
    runner.start(pipe);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    runner.stop();

    // ~10 тиков за 200 мс; границы щедрые — ловим порядок величины, не шум.
    EXPECT_GT(m.passes.load(), 2);
    EXPECT_LT(m.passes.load(), 40);
}

TEST(PipelineRunner, SpinningThreadIteratesWithoutSleep) {
    atp::pipeline pipe;
    class idle_counter : public atp::module<atp::io::inputs, atp::io::outputs> {
       public:
        std::atomic<int> passes{0};
        atp::work_status iterate(std::stop_token) override {
            ++passes;
            return atp::work_status::idle;  // spinning игнорирует idle — не спит
        }
    };
    atp::group& g = pipe.root().add_group("hot");
    idle_counter& m = g.make<idle_counter>("m");

    atp::pipeline_runner runner;
    runner.add_thread("hot", {atp::thread_mode::spinning});
    runner.assign(g, "hot");
    runner.start(pipe);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    runner.stop();

    EXPECT_GT(m.passes.load(), 1000);  // idle-модуль, но поток крутится
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
#include <atp/thread_name.hpp>

namespace atp {

// Режим темпа потока: on_demand — сон по простою (busy/idle от iterate),
// throttled — фиксированный период тиков, spinning — без сна (латентно-
// критичное). Именованные потоки — ещё и диагностика: имя уходит в ОС и
// в тексты ошибок валидации.
enum class thread_mode { on_demand, throttled, spinning };

struct thread_options {
    thread_mode mode = thread_mode::on_demand;
    std::chrono::milliseconds period{};  // throttled: целевой период итераций
};

// Исполнитель пайплайна: именованные потоки + жизненный цикл через корень
// композита. Владеет только потоками и конфигурацией; пайплайн — по ссылке
// на время работы. Все методы управления (add_thread/assign/start/stop/
// wait/running/error) — только с потока-владельца; из потоков пула и кода
// модулей раннером управлять нельзя. Конкурентный stop() во время wait()
// исключён контрактом, а не синхронизацией.
class pipeline_runner {
   public:
    pipeline_runner() = default;

    pipeline_runner(const pipeline_runner&) = delete;
    pipeline_runner& operator=(const pipeline_runner&) = delete;

    ~pipeline_runner() {
        stop();
    }

    // Темп простоя on_demand: первый простойный пасс — yield, дальше сон с
    // удвоением до потолка; busy сбрасывает. Умолчания — параметры режима.
    static constexpr auto idle_sleep_initial = std::chrono::milliseconds(1);
    static constexpr auto idle_sleep_cap = std::chrono::milliseconds(10);

    // Потоки объявляются до старта; порядок объявления значим: корень без
    // назначения исполняется первым объявленным потоком.
    void add_thread(std::string name, thread_options options = {}) {
        if (running_) {
            throw std::logic_error("cannot add threads while pipeline is running");
        }
        if (options.mode == thread_mode::throttled && options.period <= std::chrono::milliseconds::zero()) {
            throw std::invalid_argument("throttled thread '" + name + "' requires a positive period");
        }
        if (options.mode != thread_mode::throttled && options.period != std::chrono::milliseconds::zero()) {
            throw std::invalid_argument("thread '" + name + "': period is only for throttled mode");
        }
        if (index_of(name)) {
            throw std::runtime_error("duplicate thread name '" + name + "'");
        }
        threads_config_.push_back({std::move(name), options});
    }

    // Раскладка «группа → поток» — конфигурация развёртывания. Неизвестное
    // имя потока отклоняется сразу: add_thread объявляется раньше assign.
    void assign(const group& g, const std::string& thread_name) {
        if (running_) {
            throw std::logic_error("cannot assign while pipeline is running");
        }
        auto index = index_of(thread_name);
        if (!index) {
            throw std::invalid_argument("unknown thread '" + thread_name + "'");
        }
        assigned_[&g] = *index;
    }

    void start(pipeline& p) {
        if (running_) {
            throw std::logic_error("pipeline is already running");
        }
        if (threads_config_.empty()) {
            threads_config_.push_back({"main", {}});  // пустая конфигурация = однопоточный пайплайн
        }
        {
            std::lock_guard lock(error_mutex_);
            error_ = nullptr;
        }
        pipeline_ = &p;
        // Шаг 1: чистые проверки конфигурации — до каскадов, откатывать нечего.
        build_thread_map(p.root());
        map_ports(p.root());
        validate_connections(p.root());
        apply_detach(p.root());
        try {
            // initialize при сбое откатывается сам (локальный fail-fast групп);
            // сбой start требует внешнего stop: прошедшие initialize без start —
            // контракт module_base.
            p.root().initialize(p.context());
            try {
                p.root().start(p.context());
            } catch (...) {
                try {
                    p.root().stop(p.context());
                } catch (...) {  // NOLINT(bugprone-empty-catch)
                }
                throw;
            }
        } catch (...) {
            undo_detach();
            reset_state();
            throw;
        }
        launch_threads();
        running_ = true;
    }

    // Идемпотентен и не бросает: ошибки stop-каскада — через error() (Task 10).
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
    struct thread_config {
        std::string name;
        thread_options options;
    };

    [[nodiscard]] std::optional<std::size_t> index_of(const std::string& name) const {
        for (std::size_t i = 0; i < threads_config_.size(); ++i) {
            if (threads_config_[i].name == name) {
                return i;
            }
        }
        return std::nullopt;
    }

    void build_thread_map(const group& root) {
        thread_of_.clear();
        std::size_t matched = 0;
        map_group(root, 0, matched);
        // Каждое назначение обязано найтись в дереве: молча проигнорированная
        // «чужая» группа — незамеченная ошибка конфигурации.
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
        for (const group::child& c : g.children()) {
            if (auto* sub = dynamic_cast<group*>(c.module.get())) {
                map_group(*sub, index, matched);
            }
        }
    }

    // Карта «порт → поток»: владеемые порты каждого модуля получают поток
    // его исполняющей группы; реестры групп содержат одни алиасы и
    // выпадают сами.
    void map_ports(const group& g) {
        if (&g == &pipeline_->root()) {
            port_thread_.clear();
        }
        const std::size_t index = thread_of_.at(&g);
        for (const group::child& c : g.children()) {
            if (auto* sub = dynamic_cast<group*>(c.module.get())) {
                map_ports(*sub);
                continue;
            }
            for (io::input_base* port : c.module->inputs().owned()) {
                port_thread_[port] = index;
            }
            for (io::output_base* port : c.module->outputs().owned()) {
                port_thread_[port] = index;
            }
        }
    }

    // Критерий — граница потоков, не групп: соседям по потоку safe не нужен.
    void validate_connections(const group& g) const {
        for (const group::connection& c : g.connections()) {
            const std::size_t out_thread = port_thread_.at(c.out);
            const std::size_t in_thread = port_thread_.at(c.in);
            if (out_thread != in_thread && !c.in->thread_safe()) {
                throw std::runtime_error("cross-thread connection into unsafe input '" + c.in->name() +
                                         "' between threads '" + threads_config_[out_thread].name + "' and '" +
                                         threads_config_[in_thread].name + "'");
            }
        }
        for (const group::child& c : g.children()) {
            if (const auto* sub = dynamic_cast<const group*>(c.module.get())) {
                validate_connections(*sub);
            }
        }
    }

    // Назначенные подгруппы исполняются своими потоками — из iterate
    // родителей они исключаются на время работы.
    void apply_detach(group& g) {
        for (const group::child& c : g.children()) {
            if (auto* sub = dynamic_cast<group*>(c.module.get())) {
                if (assigned_.contains(sub)) {
                    g.set_detached(*sub, true);
                    detached_.push_back({&g, sub});
                }
                apply_detach(*sub);
            }
        }
    }

    void undo_detach() {
        for (auto& [parent, sub] : detached_) {
            parent->set_detached(*sub, false);
        }
        detached_.clear();
    }

    // Единицы исполнения потока: корень (его умолчание — первый объявленный
    // поток) + явно назначенные группы этого потока, в DFS-порядке.
    // Неконстантные указатели: циклы потоков зовут iterate — модули мутируют.
    void collect_units(group& g, std::vector<std::vector<group*>>& per_thread) {
        if (&g == &pipeline_->root() || assigned_.contains(&g)) {
            per_thread[thread_of_.at(&g)].push_back(&g);
        }
        for (const group::child& c : g.children()) {
            if (auto* sub = dynamic_cast<group*>(c.module.get())) {
                collect_units(*sub, per_thread);
            }
        }
    }

    void launch_threads() {
        stop_source_ = {};  // свежий источник: раннер мог уже отработать цикл
        std::vector<std::vector<group*>> per_thread(threads_config_.size());
        collect_units(pipeline_->root(), per_thread);
        for (std::size_t i = 0; i < threads_config_.size(); ++i) {
            if (per_thread[i].empty()) {
                continue;  // пустому потоку нечего делать — не создаём
            }
            const thread_config& config = threads_config_[i];
            threads_.emplace_back([this, config, units = std::move(per_thread[i])] {
                detail::set_current_thread_name(config.name);
                run_loop(units, config.options);
            });
        }
    }

    [[nodiscard]] work_status iterate_units(const std::vector<group*>& units, const std::stop_token& token) {
        work_status pass = work_status::idle;
        for (group* g : units) {
            if (g->iterate(token) == work_status::busy) {
                pass = work_status::busy;
            }
        }
        return pass;
    }

    void run_loop(const std::vector<group*>& units, const thread_options& options) {
        std::stop_token token = stop_source_.get_token();
        // Сон прерываем стоп-токеном: request_stop (ошибка или stop()) будит
        // мгновенно, спящий поток не оттягивает остановку.
        std::mutex sleep_mutex;
        std::condition_variable_any sleep_cv;
        std::chrono::milliseconds delay{};  // on_demand: 0 — ещё не спим, только yield
        try {
            while (!token.stop_requested()) {
                const work_status pass = iterate_units(units, token);
                switch (options.mode) {
                    case thread_mode::spinning:
                        std::this_thread::yield();
                        break;
                    case thread_mode::throttled: {
                        // скольжение: пропущенные тики не навёрстываем — темп ровный
                        const auto next = std::chrono::steady_clock::now() + options.period;
                        std::unique_lock lock(sleep_mutex);
                        sleep_cv.wait_until(lock, token, next, [] { return false; });
                        break;
                    }
                    case thread_mode::on_demand:
                        if (pass == work_status::busy) {
                            delay = {};
                            break;
                        }
                        if (delay == std::chrono::milliseconds{}) {
                            std::this_thread::yield();
                            delay = idle_sleep_initial;
                            break;
                        }
                        {
                            std::unique_lock lock(sleep_mutex);
                            sleep_cv.wait_for(lock, token, delay, [] { return false; });
                        }
                        delay = std::min(delay * 2, idle_sleep_cap);
                        break;
                }
            }
        } catch (...) {
            capture_error(std::current_exception());
        }
    }

    // Первая ошибка побеждает; остановка — общая на весь пайплайн.
    // request_stop под замком: он входит в предикат wait() (Task 10),
    // изменение условия вне мьютекса — потерянное пробуждение.
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

    // Общий хвост stop()/wait(): дождаться потоков, stop-каскад через корень,
    // сбросить рабочее состояние.
    void shutdown() {
        for (std::jthread& t : threads_) {
            if (t.joinable()) {
                t.join();
            }
        }
        threads_.clear();
        try {
            pipeline_->root().stop(pipeline_->context());
        } catch (...) {
            store_error(std::current_exception());  // stop() не бросает — ошибка доступна через error()
        }
        undo_detach();
        reset_state();
    }

    void reset_state() {
        thread_of_.clear();
        port_thread_.clear();
        pipeline_ = nullptr;
        running_ = false;
    }

    std::vector<thread_config> threads_config_;
    std::unordered_map<const group*, std::size_t> assigned_;
    std::unordered_map<const group*, std::size_t> thread_of_;
    std::unordered_map<const io::io_base*, std::size_t> port_thread_;
    std::vector<std::pair<group*, group*>> detached_;  // (родитель, подгруппа) — для отката
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

Include-блок: `<optional>` (index_of).

- [ ] **Step 5: Тесты зелёные** (`PipelineRunner.*`, `[  PASSED  ] 9 tests`; полный `ctest` — 100%).

- [ ] **Step 6: Предложить пользователю коммит**

Сообщение: `add pipeline_runner: named threads with modes, port-thread validation, cascades via composite root`

---

### Task 10: `pipeline_runner` — ошибки исполнения, `wait()/error()`, сквозной поток данных

**Files:**
- Modify: `src/atp/pipeline_runner.hpp`
- Test: `tests/pipeline_runner_tests.cpp`

**Interfaces:**
- Consumes: `capture_error/store_error/shutdown/error_cv_` (Task 9).
- Produces:
  - `void wait()` — «работать до аварии»: блокируется на CV до первой ошибки исполнения (у здорового пайплайна — бессрочно), выполняет остановку, **перебрасывает** первопричину; перебрасывает и на уже остановленном пайплайне, пока ошибка хранится (до следующего `start()`)
  - `std::exception_ptr error() const` — nullptr, если ошибок не было

- [ ] **Step 1: Падающие тесты**

```cpp
TEST(PipelineRunner, IterateFailureStopsPipelineAndWaitRethrows) {
    rig r;
    r.b->throw_in = "iterate";

    atp::pipeline_runner runner;
    runner.add_thread("main");
    runner.add_thread("aux");
    runner.assign(*r.stage, "aux");
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

    atp::pipeline_runner runner;
    runner.add_thread("main");
    runner.add_thread("aux");
    runner.assign(*r.stage, "aux");
    runner.start(r.pipe);
    EXPECT_THROW(runner.wait(), std::runtime_error);
    EXPECT_NE(runner.error(), nullptr);                // слот заполнен один раз
}

TEST(PipelineRunner, StopIsIdempotentAndErrorIsClean) {
    rig r;
    std::latch ticked(1);
    r.a->first_iterate = &ticked;

    atp::pipeline_runner runner;
    runner.start(r.pipe);
    ticked.wait();
    runner.stop();
    runner.stop();                                     // второй вызов — no-op
    EXPECT_EQ(runner.error(), nullptr);
}

TEST(PipelineRunner, WaitAfterStopRethrowsPendingError) {
    rig r;
    std::latch reached(1);
    r.a->first_iterate = &reached;   // зонд сигналит latch до броска (см. probe_module)
    r.a->throw_in = "iterate";

    atp::pipeline_runner runner;
    runner.start(r.pipe);
    reached.wait();
    runner.stop();                                     // не бросает; после join ошибка захвачена
    EXPECT_THROW(runner.wait(), std::runtime_error);   // stop() не съел первопричину
    EXPECT_NE(runner.error(), nullptr);
}

TEST(PipelineRunner, WaitOnIdleRunnerIsNoOp) {
    atp::pipeline_runner runner;
    runner.wait();                                     // не стартовал, ошибки нет — сразу возврат
    EXPECT_EQ(runner.error(), nullptr);
}

TEST(PipelineRunner, SecondWaitRethrowsSameError) {
    rig r;
    r.b->throw_in = "iterate";

    atp::pipeline_runner runner;
    runner.start(r.pipe);
    EXPECT_THROW(runner.wait(), std::runtime_error);
    EXPECT_THROW(runner.wait(), std::runtime_error);   // ошибка хранится до следующего start()
}

TEST(PipelineRunner, DestructorStopsRunningPipeline) {
    rig r;
    std::latch ticked(1);
    r.a->first_iterate = &ticked;
    {
        atp::pipeline_runner runner;
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

    atp::pipeline_runner runner;
    runner.add_thread("producing");
    runner.add_thread("consuming");
    runner.assign(left, "producing");
    runner.assign(right, "consuming");
    runner.start(pipe);
    delivered.wait();
    runner.stop();

    EXPECT_EQ(c.inputs().value.get(), 42);
}
```

- [ ] **Step 2: Не компилируется** (нет `wait/error`).

- [ ] **Step 3: Реализация** — в публичную часть `pipeline_runner` (после `stop()`):

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

- [ ] **Step 4: Тесты зелёные** (`PipelineRunner.*`, `[  PASSED  ] 17 tests`), затем полный `ctest` — 100%.

- [ ] **Step 5: Предложить пользователю коммит**

Сообщение: `pipeline_runner: execution errors, cv-based wait()/error(), cross-thread data flow`

---

### Task 11: демо-пайплайн + финальная сверка

**Files:**
- Modify: `examples/demo/main.cpp` (переписать на пайплайн)
- Modify: `examples/demo/CMakeLists.txt` (линковать `atp_host`)
- Modify: `docs/2026-07-11-pipeline-platform-resume.md` — отметить завершение исполнения плана

**Interfaces:**
- Consumes: всё из Tasks 5–10.

- [ ] **Step 1: `examples/demo/CMakeLists.txt`**

```cmake
# Демо-приложение платформы. Заголовки библиотеки указаны в сорцах цели —
# только для видимости в IDE (глобы определены в корневом CMakeLists.txt).
add_executable(atp_demo main.cpp ${ATP_PLATFORM_HEADERS} ${ATP_HOST_HEADERS})
target_link_libraries(atp_demo PRIVATE atp_host)
```

- [ ] **Step 2: `examples/demo/main.cpp` — мини-пайплайн на именованных потоках**

```cpp
#include <chrono>
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

    // Раскладка развёртывания: именованные потоки с режимами.
    atp::pipeline_runner runner;
    runner.add_thread("producing");                                                     // on_demand
    runner.add_thread("consuming", {atp::thread_mode::throttled, std::chrono::milliseconds(5)});
    runner.assign(producers, "producing");
    runner.assign(consumers, "consuming");
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
cmake --build cmake-build-debug --target atp_demo
cmake-build-debug\examples\demo\atp_demo.exe
```
Ожидание: строки `sink received: 1..3` (порядок значений строгий) и `queued history: 1 2 3`.

- [ ] **Step 4: Полный прогон всех тестов** (100% passed).

- [ ] **Step 5: Отметить завершение** в `docs/2026-07-11-pipeline-platform-resume.md` и предложить финальный коммит.

Сообщение: `demo: nested groups pipeline on named threads (on_demand + throttled)`

---

## Self-review плана

- **Покрытие спек:** thread_safe (T1); io-реестры через базу + work_status-алиас + ABI-комментарий (T2); alias/owned в io_registry (T3); src/-раскладка atp_host (T4); группа-композит: состав/каскады с локальным откатом/stop-продолжает/iterate-агрегация/detach (T5), экспорт алиасами в собственные реестры + одна ветка разрешения путей (T6), connect с записями {out,in} + авторазрыв (T7); pipeline-агрегат (T8); раннер: именованные потоки, три режима (скольжение throttled, прерываемые сны), неявный "main", корень → первый объявленный, inline-наследование, detach, карта порт→поток от owned(), валидация с именами потоков, каскады через root, fail-fast (T9); ошибки/первая побеждает/CV-wait/переброс после stop/повторный wait/идемпотентный stop/деструктор/межпоточные данные через watcher (T10); демо (T11). Контракты stop/wait и iterate-idle — по своим спекам (стоп/wait без изменений; константы backoff — параметры on_demand).
- **Типы согласованы:** `child{name, module, detached}` (T5) используется map_group/map_ports/collect_units (T9); `connection{out, in}` (T7) — валидацией через port_thread_ (T9); `probe_module(event_log&, std::string)` (T5) — тестами T5/T9/T10; `thread_options{mode, period}` (T9) — тестами и демо (T11); `work_status`-агрегация группы (T5) — циклами run_loop (T9).
- **Плейсхолдеров нет:** каждый шаг с полным кодом/командой.
