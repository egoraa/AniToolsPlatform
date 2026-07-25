# Проперти модулей — план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Цель:** третий вид декларируемых сущностей модуля — проперти: типизированные значения-настройки, редактируемые на лету (studio, `atp_app -p`), читаемые модулем pull-моделью; заменяют `params`/`module_config`.

**Архитектура:** переиспользование io-машинерии (`io_base`, `io_registry`): `property_base`/`property<T>` + секция-реестр `properties`, третья секция узла `ports<TIn, TOut, TProps>`. Конвертация — трейт `property_codec<T>`. **Перечисление** — не отдельный вид проперти, а ограничение набором значений: непустой `property_base::options()` (канонические строки в порядке объявления). Объявляется двумя путями, для потребителей неразличимыми: таблица имён `enum_names<E>` — «перечисление на уровне типа» (C++ enum), `option_set`/`allowed(...)` в объявлении порта — «перечисление на уровне экземпляра» (любой тип: `int`, `std::string`, подмножество enum). `property_kind` при этом остаётся ровно тем, чем был, — JSON-типом значения. Конфиг: узел `properties` (JSON-скаляры) вместо `params`, схема 1.0 → 1.1. Спека: `.superpowers/specs/2026-07-24-module-properties-design.md` (локальный артефакт brainstorming, в git не входит; ключевые решения продублированы в этом плане).

**Стек:** C++23, header-only, GoogleTest 1.17, nlohmann_json (только рантайм), Qt (studio GUI).

## Global Constraints

- Комментарии в коде и общение — на русском; стиль `docs/code_style.md` + `.clang-format` (отступ 4, 120 колонок, фигурные скобки обязательны).
- Платформа (`include/atp/`) не зависит от nlohmann_json; include-стиль `<atp/...>`.
- Сборка и запуск тестов из shell — только пресетами CMake (см. «Команды» ниже); генератор «Visual Studio 17 2022» находит тулчейн сам, обёртка vcvars не нужна.
- **Git-коммиты НЕ выполнять**: в конце каждой задачи — стоп-точка «показать изменения пользователю», коммитит пользователь сам. Шаги сборки/тестов разрешены самим фактом запуска этого плана.
- Новые тест-файлы добавлять в `tests/CMakeLists.txt` (алфавитный список `add_executable(atp_tests ...)`).

**Команды (единственный разрешённый способ собрать/прогнать):**

```powershell
# конфигурация — один раз (и после добавления файлов в tests/CMakeLists.txt)
cmake --preset windows-msvc
# сборка всего
cmake --build --preset windows-msvc-debug
# запуск группы тестов
build\windows-msvc\tests\Debug\atp_tests.exe --gtest_filter="PropertyCodec.*"
# полный прогон (финал задачи)
build\windows-msvc\tests\Debug\atp_tests.exe --gtest_brief=1
```

---

### Task 1: property_codec — трейт конвертации

**Files:**
- Create: `include/atp/io/property_codec.hpp`
- Create: `tests/property_codec_tests.cpp`
- Modify: `tests/CMakeLists.txt` (строка `property_codec_tests.cpp` в список после `ports_tests.cpp`)

**Interfaces:**
- Produces: `atp::io::property_kind { number, boolean, text }`; `atp::io::property_codec<T>` со статиками `kind` (property_kind), `to_string(const T&) -> std::string`, `from_string(std::string_view) -> std::optional<T>`; концепт `atp::io::property_value<T>`. Специализации: целые, плавающие, `bool`, `std::string`.

- [ ] **Step 1: написать падающие тесты**

`tests/property_codec_tests.cpp`:

```cpp
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include <atp/io/property_codec.hpp>

namespace {

// Пользовательский тип с собственной специализацией кодека.
struct percent {
    int value = 0;
};

}  // namespace

// Специализация в atp::io — тот же способ, что у встроенных типов.
template <>
struct atp::io::property_codec<percent> {
    static constexpr atp::io::property_kind kind = atp::io::property_kind::text;
    static std::string to_string(const percent& p) {
        return std::to_string(p.value) + "%";
    }
    static std::optional<percent> from_string(std::string_view text) {
        if (text.empty() || text.back() != '%') {
            return std::nullopt;
        }
        return percent{std::stoi(std::string(text.substr(0, text.size() - 1)))};
    }
};

namespace {

TEST(PropertyCodec, IntRoundTrip) {
    EXPECT_EQ(atp::io::property_codec<int>::to_string(-42), "-42");
    EXPECT_EQ(atp::io::property_codec<int>::from_string("-42"), std::optional(-42));
    EXPECT_EQ(atp::io::property_codec<int>::kind, atp::io::property_kind::number);
}

TEST(PropertyCodec, IntRejectsGarbage) {
    EXPECT_EQ(atp::io::property_codec<int>::from_string("abc"), std::nullopt);
    EXPECT_EQ(atp::io::property_codec<int>::from_string("12x"), std::nullopt);  // хвост — тоже отказ
    EXPECT_EQ(atp::io::property_codec<int>::from_string(""), std::nullopt);
}

TEST(PropertyCodec, DoubleRoundTrip) {
    const std::string text = atp::io::property_codec<double>::to_string(0.5);
    EXPECT_EQ(atp::io::property_codec<double>::from_string(text), std::optional(0.5));
}

TEST(PropertyCodec, BoolIsWordsTrueFalse) {
    EXPECT_EQ(atp::io::property_codec<bool>::to_string(true), "true");
    EXPECT_EQ(atp::io::property_codec<bool>::from_string("false"), std::optional(false));
    EXPECT_EQ(atp::io::property_codec<bool>::from_string("TRUE"), std::nullopt);  // без вольностей регистра
    EXPECT_EQ(atp::io::property_codec<bool>::kind, atp::io::property_kind::boolean);
}

TEST(PropertyCodec, StringIsIdentity) {
    EXPECT_EQ(atp::io::property_codec<std::string>::to_string("a b"), "a b");
    EXPECT_EQ(atp::io::property_codec<std::string>::from_string("a b"), std::optional<std::string>("a b"));
    EXPECT_EQ(atp::io::property_codec<std::string>::kind, atp::io::property_kind::text);
}

TEST(PropertyCodec, UserSpecializationSatisfiesConcept) {
    static_assert(atp::io::property_value<percent>);
    static_assert(atp::io::property_value<int>);
    static_assert(!atp::io::property_value<void*>);  // без специализации типу отказано
    EXPECT_EQ(atp::io::property_codec<percent>::from_string("15%")->value, 15);
}

}  // namespace
```

- [ ] **Step 2: собрать, убедиться что сборка падает** (нет заголовка). Команда сборки из «Команд»; ожидание: ошибка компиляции `property_codec.hpp: No such file`.

- [ ] **Step 3: реализовать заголовок**

`include/atp/io/property_codec.hpp`:

```cpp
#ifndef ANITOOLSPLATFORM_IO_PROPERTY_CODEC_HPP
#define ANITOOLSPLATFORM_IO_PROPERTY_CODEC_HPP

#include <charconv>
#include <concepts>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace atp::io {

// Вид значения проперти — подсказка сериализаторам, живущим над строкой:
// рантайм по нему пишет в конфиг число, а не "число". Это JSON-тип и только:
// «одно из набора» — свойство ортогональное (см. property_base::options()),
// им ограничены бывают и числа, и строки, и enum, а тип записи от этого не
// меняется. Виджет инспектор выбирает по обоим признакам.
enum class property_kind { number, boolean, text };

// Трейт конвертации значения проперти в строку и обратно. Primary-определения
// нет: тип без специализации — ошибка компиляции, а не тихая деградация.
// from_string возвращает nullopt на непарсящейся строке — исключение с
// контекстом (имя проперти) бросает property_base, кодек контекста не знает.
template <typename T>
struct property_codec;

namespace detail {

// Числа — через from_chars/to_chars: локаленезависимо и без аллокаций.
// Частичный разбор ("12x") — отказ: строка обязана быть числом целиком.
template <typename T>
std::optional<T> parse_number(std::string_view text) {
    T value{};
    const char* end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(text.data(), end, value);
    if (ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return value;
}

template <typename T>
std::string print_number(T value) {
    char buffer[64];  // с запасом для любых арифметических типов
    const auto [ptr, ec] = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return std::string(buffer, ptr);
}

}  // namespace detail

// Целые; bool исключён — у него собственная текстовая форма ниже.
template <std::integral T>
    requires(!std::same_as<T, bool>)
struct property_codec<T> {
    static constexpr property_kind kind = property_kind::number;
    static std::string to_string(const T& value) {
        return detail::print_number(value);
    }
    static std::optional<T> from_string(std::string_view text) {
        return detail::parse_number<T>(text);
    }
};

// Плавающие: to_chars даёт кратчайшую строку с точным round-trip.
template <std::floating_point T>
struct property_codec<T> {
    static constexpr property_kind kind = property_kind::number;
    static std::string to_string(const T& value) {
        return detail::print_number(value);
    }
    static std::optional<T> from_string(std::string_view text) {
        return detail::parse_number<T>(text);
    }
};

template <>
struct property_codec<bool> {
    static constexpr property_kind kind = property_kind::boolean;
    static std::string to_string(const bool& value) {
        return value ? "true" : "false";
    }
    static std::optional<bool> from_string(std::string_view text) {
        if (text == "true") {
            return true;
        }
        if (text == "false") {
            return false;
        }
        return std::nullopt;
    }
};

template <>
struct property_codec<std::string> {
    static constexpr property_kind kind = property_kind::text;
    static std::string to_string(const std::string& value) {
        return value;
    }
    static std::optional<std::string> from_string(std::string_view text) {
        return std::string(text);
    }
};

// Контракт «тип пригоден для property<T>»: полная тройка kind/to/from.
template <typename T>
concept property_value = requires(const T& value, std::string_view text) {
    { property_codec<T>::kind } -> std::convertible_to<property_kind>;
    { property_codec<T>::to_string(value) } -> std::same_as<std::string>;
    { property_codec<T>::from_string(text) } -> std::same_as<std::optional<T>>;
};

}  // namespace atp::io

#endif  // ANITOOLSPLATFORM_IO_PROPERTY_CODEC_HPP
```

- [ ] **Step 4: собрать и прогнать** `--gtest_filter="PropertyCodec.*"`. Ожидание: все PASS.
- [ ] **Step 5: стоп-точка** — показать пользователю дифф задачи (без коммита).

---

### Task 1b: enum_names — перечисление на уровне типа

**Files:**
- Create: `include/atp/io/enum_names.hpp`
- Create: `tests/enum_names_tests.cpp`
- Modify: `tests/CMakeLists.txt` (строка `enum_names_tests.cpp` в алфавитный список)

**Interfaces:**
- Consumes: `property_codec`/`property_kind` (Task 1).
- Produces: `atp::io::enum_entry<E> { E value; std::string_view name; }`; точка кастомизации `atp::io::enum_names<E>` (primary без определения); концепт `atp::io::named_enum<E>`; частичная специализация `property_codec<E>` для `named_enum` — `kind == property_kind::text` (имя варианта едет в конфиг строкой, как `mode` у потоков) плюс один статик сверх общего контракта кодека: `options() -> std::span<const std::string_view>` — допустимые имена в порядке объявления. Task 3 подхватывает его `requires`-проверкой, поэтому скалярных кодеков он не касается; проверку «значение внутри набора» делает там же `property<T>`, кодеку она не нужна.

- [ ] **Step 1: написать падающие тесты**

`tests/enum_names_tests.cpp`:

```cpp
#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include <atp/io/enum_names.hpp>

namespace {

// Порядок вариантов намеренно не совпадает с алфавитным: таблица задаёт
// порядок выпадающего списка, и он обязан сохраняться как объявлен.
enum class blend { normal, add, multiply };

// Enum без таблицы имён — проверка, что концепт его не пускает.
enum class nameless { a, b };

}  // namespace

// Специализация в atp::io — тот же способ, что у property_codec<percent>.
template <>
struct atp::io::enum_names<blend> {
    static constexpr std::array entries{
        atp::io::enum_entry{blend::normal, "normal"},
        atp::io::enum_entry{blend::add, "add"},
        atp::io::enum_entry{blend::multiply, "multiply"},
    };
};

namespace {

using blend_codec = atp::io::property_codec<blend>;

TEST(EnumNames, ConceptAcceptsOnlyTabledEnums) {
    static_assert(atp::io::named_enum<blend>);
    static_assert(!atp::io::named_enum<nameless>);
    static_assert(!atp::io::named_enum<int>);
    static_assert(atp::io::property_value<blend>);  // общий контракт кодека выполнен
}

TEST(EnumNames, RoundTripsThroughNames) {
    EXPECT_EQ(blend_codec::to_string(blend::multiply), "multiply");
    EXPECT_EQ(blend_codec::from_string("add"), std::optional(blend::add));
    EXPECT_EQ(blend_codec::kind, atp::io::property_kind::text);  // в конфиг имя едет строкой
}

TEST(EnumNames, UnknownNameIsRejected) {
    EXPECT_EQ(blend_codec::from_string("screen"), std::nullopt);
    EXPECT_EQ(blend_codec::from_string(""), std::nullopt);
    EXPECT_EQ(blend_codec::from_string("Add"), std::nullopt);  // регистр значим, как у bool
    EXPECT_EQ(blend_codec::from_string("1"), std::nullopt);    // числовая форма не принимается
}

TEST(EnumNames, OptionsKeepDeclarationOrder) {
    const std::span<const std::string_view> options = blend_codec::options();
    ASSERT_EQ(options.size(), 3u);
    EXPECT_EQ(options[0], "normal");
    EXPECT_EQ(options[1], "add");
    EXPECT_EQ(options[2], "multiply");
    static_assert(blend_codec::options().size() == 3);  // список доступен и на этапе компиляции
}

// Значение вне таблицы — ошибка модуля; кодек её не диагностирует (у него нет
// имени проперти для сообщения), но и не выдумывает текст: пустая строка не
// совпадёт ни с одним допустимым именем, и property<E> отвергнет запись.
TEST(EnumNames, ValueOutsideTableHasNoText) {
    EXPECT_EQ(blend_codec::to_string(static_cast<blend>(99)), "");
}

}  // namespace
```

- [ ] **Step 2: собрать, убедиться что сборка падает** (нет `enum_names.hpp`).

- [ ] **Step 3: реализовать заголовок**

`include/atp/io/enum_names.hpp`:

```cpp
#ifndef ANITOOLSPLATFORM_IO_ENUM_NAMES_HPP
#define ANITOOLSPLATFORM_IO_ENUM_NAMES_HPP

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>

#include <atp/io/property_codec.hpp>

namespace atp::io {

// Строка таблицы имён: значение enum и его текстовая форма. Имя — и токен
// конфига, и метка в выпадающем списке studio: разделять их незачем, пока
// у нас нет локализации (прецедент — "on_demand" у режимов потоков).
template <typename E>
struct enum_entry {
    E value;
    std::string_view name;
};

// Точка кастомизации: модуль специализирует её для своего enum, объявляя
//     static constexpr std::array entries{enum_entry{E::a, "a"}, ...};
// Primary-определения нет — enum без таблицы получает ошибку компиляции,
// а не молчаливую деградацию до числового или текстового кодека.
template <typename E>
struct enum_names;

template <typename E>
concept named_enum = std::is_enum_v<E> && requires { enum_names<E>::entries; };

namespace detail {

// Имена отдельным массивом: options() отдаёт span, а entries хранит пары.
// Переменная-шаблон своя в каждой DLL — как у erased_of<T>(), поэтому
// сравнивается только содержимое string_view, никогда не адреса.
template <named_enum E>
inline constexpr auto enum_option_names = [] {
    constexpr std::size_t count = std::tuple_size_v<std::remove_cvref_t<decltype(enum_names<E>::entries)>>;
    std::array<std::string_view, count> names{};
    for (std::size_t i = 0; i < count; ++i) {
        names[i] = enum_names<E>::entries[i].name;
    }
    return names;
}();

}  // namespace detail

// Кодек любого enum с таблицей имён. Поиск линейный: таблицы измеряются
// единицами строк, а порядок объявления — единственный внятный порядок для
// списка вариантов (сортировка или хеш-таблица его бы разрушили).
template <named_enum E>
struct property_codec<E> {
    // Имя варианта — обычная строка: в конфиге enum неотличим от текстовой
    // проперти, «одно из» выражает не kind, а непустой options().
    static constexpr property_kind kind = property_kind::text;

    static std::string to_string(const E& value) {
        for (const enum_entry<E>& e : enum_names<E>::entries) {
            if (e.value == value) {
                return std::string(e.name);
            }
        }
        return {};  // недостижимо: property<E> не пускает внутрь значение вне таблицы
    }

    static std::optional<E> from_string(std::string_view text) {
        for (const enum_entry<E>& e : enum_names<E>::entries) {
            if (e.name == text) {
                return e.value;
            }
        }
        return std::nullopt;
    }

    // Допустимые значения в порядке объявления — «перечисление на уровне
    // типа»: property<E> перенесёт их в свой набор (Task 3), дальше они
    // неотличимы от перечисленных через allowed(). Span смотрит в статику
    // (у плагина — в его DLL, пришпиленную module_deleter'ом).
    static constexpr std::span<const std::string_view> options() {
        return detail::enum_option_names<E>;
    }
};

}  // namespace atp::io

#endif  // ANITOOLSPLATFORM_IO_ENUM_NAMES_HPP
```

- [ ] **Step 4: собрать и прогнать** `--gtest_filter="EnumNames.*"`. Ожидание: все PASS.

Два места, где ожидание опирается на поведение компилятора, — проверить на первой же сборке, обходные пути известны и контракта не меняют:
  - `enum_entry{blend::normal, "normal"}` — агрегатный CTAD (C++20). Если MSVC откажется выводить, писать `enum_entry<blend>{...}`.
  - `!named_enum<nameless>` — концепт опирается на то, что обращение к члену неполного `enum_names<E>` даёт substitution failure, а не hard error. Если жёсткая ошибка всё-таки будет, определить primary пустым (`template <typename E> struct enum_names {};`): отсутствие таблицы всё равно останется ошибкой компиляции, только придёт она от `property_value<E>`, а не от самого `enum_names`.

- [ ] **Step 5: стоп-точка** — показать пользователю дифф задачи (без коммита).

---

### Task 2: io_registry::make — вариадик

**Files:**
- Modify: `include/atp/io/io_registry.hpp:32-44` (метод `make`)

**Interfaces:**
- Produces: `template <std::derived_from<TBase> TItem, typename... TArgs> TItem& make(std::string name, TArgs&&... args)` — конструирует `TItem(name, args...)`. Существующие вызовы `make<input<int>>("x")` и `make<input<int>>("x", unsafe)` не меняются.

- [ ] **Step 1: заменить сигнатуру make**

Было:

```cpp
    template <std::derived_from<TBase> TItem>
    TItem& make(std::string name, safety s = safe) {
        auto item = std::make_unique<TItem>(name, s);
```

Стало:

```cpp
    // Хвост аргументов уходит конструктору элемента как есть: input попадает
    // на (name, safety), property — на (name, default, persistence, safety).
    template <std::derived_from<TBase> TItem, typename... TArgs>
    TItem& make(std::string name, TArgs&&... args) {
        auto item = std::make_unique<TItem>(name, std::forward<TArgs>(args)...);
```

Остальное тело без изменений (`name` дальше перемещается в `try_emplace` — копия для конструктора уже сделана выше). Обновить комментарий класса (строки 20-22): пример `make<input<int>>("number")` оставить, добавить строку про хвост аргументов.

- [ ] **Step 2: собрать и прогнать всё** (полная команда). Ожидание: все существующие тесты PASS — изменение регрессионно-нейтрально; предметный тест нового пути даст Task 4.
- [ ] **Step 3: стоп-точка** — показать дифф пользователю.

---

### Task 3: property_base + property\<T\> + перечисление на уровне экземпляра

**Files:**
- Create: `include/atp/io/property_base.hpp`
- Create: `include/atp/io/property.hpp`
- Create: `tests/property_tests.cpp`
- Modify: `tests/CMakeLists.txt` (строка `property_tests.cpp` после `property_codec_tests.cpp`)

**Interfaces:**
- Consumes: `io_base` (name/type/lock из Task 0-контекста), `property_codec<T>`/`property_value`/`property_kind` (Task 1), статик `options()` enum-кодека (Task 1b).
- Produces:
  - тег `atp::io::persistence {bool keep}`, константы `atp::io::persistent`/`atp::io::transient`;
  - `atp::io::option_set<TValue> { std::vector<TValue> values; }` и вокабуляр объявления `atp::io::allowed(values...)` (пустой набор запрещён на этапе компиляции);
  - `atp::io::property_base : io_base` — `kind() -> property_kind`, `options() -> const std::vector<std::string>&` (пусто — ограничений нет; непусто — проперть-перечисление), `persistent() -> bool`, чистые виртуалы `to_string() const -> std::string`, `from_string(std::string_view)` (бросает `std::invalid_argument`), `default_string() const -> std::string`, `changed() const -> bool`;
  - `atp::io::property<T>` — два конструктора: `(name, default = T{}, persistence = persistent, safety = safe)` и `(name, default, const option_set<TValue>&, persistence = persistent, safety = safe)`; `operator()(U&&)`, `get() -> T`, `take() -> std::optional<T>`, overrides базы + `reset()`.
  - Единый инвариант перечисления: значение проперти всегда лежит в наборе, если набор объявлен. Набор наполняется из одного из двух источников — таблицы имён типа (enum) или `option_set` экземпляра, — после чего они неразличимы. Дефолт, типизированная запись и `from_string` проверяются одинаково (`std::invalid_argument` с перечнем допустимых значений), поэтому `to_string()` не бросает никогда и весь строковый слой (конфиг, `-p`, studio) безопасен.

- [ ] **Step 1: написать падающие тесты**

`tests/property_tests.cpp`:

```cpp
#include <array>
#include <atomic>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <atp/io/enum_names.hpp>
#include <atp/io/property.hpp>

namespace {

// Enum-проперть: таблица имён рядом с enum, дальше всё как у скаляров.
enum class blend { normal, add, multiply };

// Enum, у которого нулевого варианта нет: проверка ловушки дефолта.
enum class scale { half = 1, full = 2 };

}  // namespace

template <>
struct atp::io::enum_names<blend> {
    static constexpr std::array entries{
        atp::io::enum_entry{blend::normal, "normal"},
        atp::io::enum_entry{blend::add, "add"},
        atp::io::enum_entry{blend::multiply, "multiply"},
    };
};

template <>
struct atp::io::enum_names<scale> {
    static constexpr std::array entries{
        atp::io::enum_entry{scale::half, "half"},
        atp::io::enum_entry{scale::full, "full"},
    };
};

namespace {

TEST(Property, HasDefaultValueFromBirth) {
    atp::io::property<int> limit("limit", 10);
    EXPECT_EQ(limit.get(), 10);
    EXPECT_FALSE(limit.changed());
    EXPECT_EQ(limit.take(), std::nullopt);  // дефолт — не событие
}

TEST(Property, DefaultOfDefaultIsValueInitialized) {
    atp::io::property<int> p("p");
    EXPECT_EQ(p.get(), 0);
}

TEST(Property, WriteRaisesChangedAndTakeConsumesIt) {
    atp::io::property<int> limit("limit", 10);
    limit(42);
    EXPECT_TRUE(limit.changed());
    EXPECT_EQ(limit.get(), 42);   // get не гасит флаг
    EXPECT_TRUE(limit.changed());
    EXPECT_EQ(limit.take(), std::optional(42));
    EXPECT_EQ(limit.take(), std::nullopt);  // событие обработано ровно раз
    EXPECT_EQ(limit.get(), 42);             // значение при этом на месте
}

TEST(Property, EveryWriteRaisesFlagEvenIfEqual) {
    atp::io::property<int> p("p", 5);
    p(5);  // то же значение — всё равно событие (сравнения нет намеренно)
    EXPECT_TRUE(p.changed());
}

TEST(Property, ResetRestoresDefaultAndRaisesFlag) {
    atp::io::property<std::string> file("file", "a.txt");
    file(std::string("b.txt"));
    (void)file.take();
    file.reset();
    EXPECT_EQ(file.get(), "a.txt");
    EXPECT_TRUE(file.changed());  // модуль должен узнать об откате
}

TEST(Property, StringAccessThroughBase) {
    atp::io::property<int> limit("limit", 10);
    atp::io::property_base& base = limit;
    EXPECT_EQ(base.to_string(), "10");
    base.from_string("77");
    EXPECT_EQ(limit.get(), 77);
    EXPECT_TRUE(limit.changed());
    EXPECT_EQ(base.default_string(), "10");
    EXPECT_EQ(base.kind(), atp::io::property_kind::number);
}

TEST(Property, FromStringGarbageThrowsWithContext) {
    atp::io::property<int> limit("limit");
    try {
        limit.from_string("abc");
        FAIL() << "expected std::invalid_argument";
    } catch (const std::invalid_argument& e) {
        EXPECT_NE(std::string(e.what()).find("limit"), std::string::npos);
        EXPECT_NE(std::string(e.what()).find("abc"), std::string::npos);
    }
    EXPECT_EQ(limit.get(), 0);       // отказ не трогает значение
    EXPECT_FALSE(limit.changed());   // и не взводит флаг
}

TEST(Property, PersistenceTagIsStored) {
    atp::io::property<int> saved("saved", 1);
    atp::io::property<int> session_only("session_only", 1, atp::io::transient);
    EXPECT_TRUE(saved.persistent());
    EXPECT_FALSE(session_only.persistent());
}

TEST(Property, TypeIndexMatchesValueType) {
    atp::io::property<int> p("p");
    EXPECT_EQ(p.type(), std::type_index(typeid(int)));
}

TEST(Property, UnrestrictedPropertyHasNoOptions) {
    atp::io::property<int> p("p");
    EXPECT_TRUE(p.options().empty());
    p(12345);  // без набора запись не ограничена ничем
    EXPECT_EQ(p.get(), 12345);
}

TEST(Property, EnumPropertyWorksLikeAnyOther) {
    atp::io::property<blend> mode("mode", blend::add);
    EXPECT_EQ(mode.get(), blend::add);
    mode(blend::multiply);
    EXPECT_EQ(mode.take(), std::optional(blend::multiply));

    atp::io::property_base& base = mode;
    EXPECT_EQ(base.kind(), atp::io::property_kind::text);  // имя варианта — строка
    EXPECT_EQ(base.to_string(), "multiply");
    EXPECT_EQ(base.default_string(), "add");
    base.from_string("normal");
    EXPECT_EQ(mode.get(), blend::normal);
}

// Таблица имён типа наполняет набор экземпляра — дальше источник неразличим.
TEST(Property, EnumTableBecomesTheOptionSet) {
    atp::io::property<blend> mode("mode");
    const atp::io::property_base& base = mode;
    EXPECT_EQ(base.options(), (std::vector<std::string>{"normal", "add", "multiply"}));
}

TEST(Property, FromStringListsOptionsInErrorMessage) {
    atp::io::property<blend> mode("mode");
    try {
        mode.from_string("screen");
        FAIL() << "expected std::invalid_argument";
    } catch (const std::invalid_argument& e) {
        const std::string message = e.what();
        EXPECT_NE(message.find("mode"), std::string::npos);
        EXPECT_NE(message.find("screen"), std::string::npos);
        EXPECT_NE(message.find("normal, add, multiply"), std::string::npos);
    }
    EXPECT_EQ(mode.get(), blend::normal);
    EXPECT_FALSE(mode.changed());
}

// Инвариант «значение всегда внутри набора»: непредставимое отвергается на
// входе, иначе to_string сломался бы у инспектора и при сохранении конфига.
TEST(Property, EnumRejectsValueOutsideTable) {
    atp::io::property<blend> mode("mode");
    EXPECT_THROW(mode(static_cast<blend>(99)), std::invalid_argument);
    EXPECT_EQ(mode.get(), blend::normal);  // отказ не трогает значение
    EXPECT_FALSE(mode.changed());
    EXPECT_THROW((atp::io::property<blend>("bad", static_cast<blend>(99))), std::invalid_argument);
}

// Дефолт по умолчанию — T{}, у enum это значение 0. Нет нулевого варианта в
// таблице — конструктор откажет сразу, а не отдаст проперть в непредставимом
// состоянии; автор модуля обязан назвать дефолт явно.
TEST(Property, EnumWithoutZeroVariantDemandsExplicitDefault) {
    EXPECT_THROW((atp::io::property<scale>("scale")), std::invalid_argument);
    EXPECT_NO_THROW((atp::io::property<scale>("scale", scale::full)));
}

// Перечисление без enum-типа: набор объявлен в месте объявления порта.
TEST(Property, AllowedRestrictsScalarProperty) {
    atp::io::property<int> channels("channels", 2, atp::io::allowed(1, 2, 6));
    EXPECT_EQ(channels.options(), (std::vector<std::string>{"1", "2", "6"}));
    EXPECT_EQ(channels.kind(), atp::io::property_kind::number);  // в конфиг всё равно числом
    channels(6);
    EXPECT_EQ(channels.get(), 6);
    EXPECT_THROW(channels(3), std::invalid_argument);
    EXPECT_EQ(channels.get(), 6);  // отказ не трогает значение
}

TEST(Property, AllowedRestrictsTextPropertyAndFromString) {
    atp::io::property<std::string> codec("codec", "h264", atp::io::allowed("h264", "h265", "av1"));
    codec.from_string("av1");
    EXPECT_EQ(codec.get(), "av1");
    try {
        codec.from_string("vp9");  // парсится (строка), но вне набора
        FAIL() << "expected std::invalid_argument";
    } catch (const std::invalid_argument& e) {
        const std::string message = e.what();
        EXPECT_NE(message.find("vp9"), std::string::npos);
        EXPECT_NE(message.find("h264, h265, av1"), std::string::npos);
    }
    EXPECT_EQ(codec.get(), "av1");
}

TEST(Property, DefaultOutsideAllowedSetIsRejected) {
    EXPECT_THROW((atp::io::property<int>("channels", 3, atp::io::allowed(1, 2, 6))), std::invalid_argument);
}

// Набор экземпляра сужает таблицу типа: модуль поддерживает не все варианты.
TEST(Property, AllowedNarrowsEnumTableToSubset) {
    atp::io::property<blend> mode("mode", blend::add, atp::io::allowed(blend::add, blend::multiply));
    EXPECT_EQ(mode.options(), (std::vector<std::string>{"add", "multiply"}));
    EXPECT_THROW(mode(blend::normal), std::invalid_argument);  // в таблице есть, в наборе нет
}

// Каноничность сравнения: "007" разбирается в 7, и именно 7 ищется в наборе.
TEST(Property, MembershipIsCheckedOnCanonicalText) {
    atp::io::property<int> level("level", 7, atp::io::allowed(7, 8));
    level.from_string("007");
    EXPECT_EQ(level.get(), 7);
}

// Конкурентная запись/чтение под дефолтной блокировкой: TSan-подобной
// проверки у нас нет, тест ловит хотя бы рваные значения и крэши.
TEST(Property, ConcurrentWritesAndReadsDoNotTear) {
    atp::io::property<int> p("p");
    std::atomic<bool> stop{false};
    std::thread writer([&] {
        for (int i = 0; !stop.load(); i = (i + 1) % 1000) {
            p(i);
        }
    });
    for (int pass = 0; pass < 10000; ++pass) {
        const int v = p.get();
        EXPECT_GE(v, 0);
        EXPECT_LT(v, 1000);
    }
    stop = true;
    writer.join();
}

}  // namespace
```

- [ ] **Step 2: собрать — ожидание: ошибка компиляции** (нет `property.hpp`).

- [ ] **Step 3: реализовать заголовки**

`include/atp/io/property_base.hpp`:

```cpp
#ifndef ANITOOLSPLATFORM_IO_PROPERTY_BASE_HPP
#define ANITOOLSPLATFORM_IO_PROPERTY_BASE_HPP

#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <utility>
#include <vector>

#include <atp/io/io_base.hpp>
#include <atp/io/property_codec.hpp>
#include <atp/io/threading.hpp>

namespace atp::io {

// Сохраняемость — свойство экземпляра проперти (в стиле safety):
// persistent-значения studio пишет в конфиг при сохранении, transient
// живут только в памяти работающего пайплайна.
struct persistence {
    bool keep;
};

inline constexpr persistence persistent{true};
inline constexpr persistence transient{false};

// Набор допустимых значений — «перечисление на уровне экземпляра»: тип
// проперти остаётся обычным (int, std::string, да и enum), а список вариантов
// объявляется там же, где сам порт. Хранит значения, а не строки: в строки их
// переводит кодек проперти, чтобы сравнение шло по канонической форме.
template <typename TValue>
struct option_set {
    std::vector<TValue> values;
};

// Вокабуляр объявления:
//     make<property<int>>("channels", 2, allowed(1, 2, 6));
//     make<property<std::string>>("codec", "h264", allowed("h264", "h265"));
// Пустой набор отвергается на этапе компиляции: «перечисление ни из чего»
// молча означало бы отсутствие ограничения — ровно противоположный смысл.
template <typename... TValues>
    requires(sizeof...(TValues) > 0)
[[nodiscard]] auto allowed(TValues&&... values) {
    using value_type = std::common_type_t<std::decay_t<TValues>...>;
    return option_set<value_type>{{static_cast<value_type>(std::forward<TValues>(values))...}};
}

// Type-erased база проперти — в одном ряду с input_base/output_base:
// имя, typeid(T) и синхронизация — из io_base. Здесь — строковый доступ
// (builder, CLI и studio правят значение, не зная T), вид значения для
// сериализаторов и признак сохраняемости. Проперти не подключаются к
// выходам — это отдельный вид сущности со своим реестром (properties).
class property_base : public io_base {
   public:
    property_base(std::string name,
                  std::type_index type,
                  property_kind kind,
                  std::vector<std::string> options,
                  persistence p,
                  safety s)
        : io_base(std::move(name), type, s), kind_(kind), options_(std::move(options)), persistent_(p.keep) {}

    [[nodiscard]] property_kind kind() const noexcept {
        return kind_;
    }

    // Допустимые значения в канонической строковой форме, в порядке
    // объявления. Пусто — ограничений нет; непусто — проперть-перечисление:
    // инспектор рисует выпадающий список, а запись вне набора отвергается.
    // Откуда набор взялся (таблица имён enum-типа или option_set экземпляра),
    // потребителям знать незачем — здесь эти два пути уже сошлись.
    // Хранится копией: набор экземпляра живёт в самой проперти, а не в
    // статике кодека, и после конструктора не меняется.
    [[nodiscard]] const std::vector<std::string>& options() const noexcept {
        return options_;
    }

    [[nodiscard]] bool persistent() const noexcept {
        return persistent_;
    }

    // Строковый доступ к значению. from_string на непарсящейся строке
    // бросает std::invalid_argument с именем проперти и самой строкой,
    // не трогая ни значение, ни флаг изменения.
    [[nodiscard]] virtual std::string to_string() const = 0;
    virtual void from_string(std::string_view text) = 0;

    // Дефолт строкой: studio сравнивает с ним текущее значение при
    // сохранении — равное дефолту в конфиг не пишется (без шума в диффах).
    [[nodiscard]] virtual std::string default_string() const = 0;

    // Неразрушающий пик «менялось ли с последнего take»; само изъятие —
    // типизированный take() наследника: без T оно бессмысленно.
    [[nodiscard]] virtual bool changed() const = 0;

   private:
    property_kind kind_;
    std::vector<std::string> options_;  // пусто у неограниченных пропертей — без аллокации
    bool persistent_;
};

}  // namespace atp::io

#endif  // ANITOOLSPLATFORM_IO_PROPERTY_BASE_HPP
```

`include/atp/io/property.hpp`:

```cpp
#ifndef ANITOOLSPLATFORM_IO_PROPERTY_HPP
#define ANITOOLSPLATFORM_IO_PROPERTY_HPP

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>
#include <vector>

#include <atp/io/property_base.hpp>
#include <atp/io/property_codec.hpp>
#include <atp/io/threading.hpp>

namespace atp::io {

namespace detail {

// Набор, заданный типом: options() объявляет только enum-кодек (таблица имён,
// см. enum_names.hpp) — requires-проверка избавляет скалярные кодеки от знания
// о нём. Заголовок enum_names.hpp здесь не нужен: специализацию приносит TU
// автора модуля.
template <property_value T>
std::vector<std::string> type_options() {
    if constexpr (requires { property_codec<T>::options(); }) {
        std::vector<std::string> result;
        result.reserve(property_codec<T>::options().size());
        for (std::string_view name : property_codec<T>::options()) {
            result.emplace_back(name);
        }
        return result;
    } else {
        return {};
    }
}

// Набор, заданный экземпляром: значения приводятся к T и печатаются кодеком —
// дальше проверка вхождения сравнивает канонические строки, поэтому "007"
// найдётся среди allowed(7, 8), а 7 и 7.0 не разъедутся.
template <property_value T, typename TValue>
std::vector<std::string> render_options(const option_set<TValue>& allowed) {
    std::vector<std::string> result;
    result.reserve(allowed.values.size());
    for (const TValue& value : allowed.values) {
        result.push_back(property_codec<T>::to_string(T(value)));
    }
    return result;
}

}  // namespace detail

// Типизированная проперть: значение-настройка модуля с дефолтом. В отличие
// от входа значение есть всегда — get() не бросает никогда. Запись зеркалит
// вход (T конструируется вне замка; на потоке пишущего пользовательский код
// не исполняется), чтение — pull-only: get() — состояние, take() — событие
// «изменилось с прошлого take». Любая запись взводит флаг изменения —
// сравнения со старым значением нет намеренно (equality не требуется).
template <property_value T>
class property : public property_base {
   public:
    // База конструируется первой, поэтому checked() уже вправе звать name() и
    // options(): сообщение об отвергнутом дефолте называет проперть и варианты.
    // Дефолт по умолчанию — T{}: у enum это значение 0, и если такого варианта
    // в таблице нет, конструктор откажет — дефолт называется явно.
    explicit property(std::string name, T default_value = T{}, persistence p = persistent, safety s = safe)
        : property_base(std::move(name), typeid(T), property_codec<T>::kind, detail::type_options<T>(), p, s),
          default_(checked(std::move(default_value))),
          value_(default_) {}

    // Перечисление на уровне экземпляра: набор объявлен здесь же, у порта.
    // Набор типа (таблица имён enum) при этом заменяется, а не дополняется —
    // так модуль сужает enum до поддерживаемого им подмножества.
    template <typename TValue>
        requires std::constructible_from<T, const TValue&>
    property(std::string name,
             T default_value,
             const option_set<TValue>& allowed,
             persistence p = persistent,
             safety s = safe)
        : property_base(std::move(name), typeid(T), property_codec<T>::kind,
                        detail::render_options<T>(allowed), p, s),
          default_(checked(std::move(default_value))),
          value_(default_) {}

    template <typename U>
        requires std::constructible_from<T, U>
    void operator()(U&& value) {
        T incoming = checked(T(std::forward<U>(value)));  // конструирование и проверка вне замка
        auto guard = lock();
        value_ = std::move(incoming);
        changed_ = true;
    }

    // Копия: ссылка наружу была бы гонкой с конкурентной записью.
    [[nodiscard]] T get() const {
        auto guard = lock();
        return value_;
    }

    // Значение, если менялось с прошлого take, со сбросом флага; иначе
    // nullopt. Пара «состояние/событие» — та же, что get/take у входа.
    [[nodiscard]] std::optional<T> take() {
        auto guard = lock();
        if (!changed_) {
            return std::nullopt;
        }
        changed_ = false;
        return value_;
    }

    [[nodiscard]] bool changed() const override {
        auto guard = lock();
        return changed_;
    }

    // Возврат к дефолту — тоже изменение: модуль должен узнать об откате.
    void reset() override {
        auto guard = lock();
        value_ = default_;
        changed_ = true;
    }

    [[nodiscard]] std::string to_string() const override {
        auto guard = lock();
        return property_codec<T>::to_string(value_);
    }

    // Два разных отказа: строка не разобралась либо разобралась в значение вне
    // набора. Проверка вхождения — та же, что у типизированной записи: обойти
    // ограничение через строковый путь нельзя.
    void from_string(std::string_view text) override {
        std::optional<T> parsed = property_codec<T>::from_string(text);  // парсинг вне замка
        if (!parsed) {
            throw std::invalid_argument("property '" + name() + "': cannot parse '" + std::string(text) + "'" +
                                        options_hint());
        }
        T incoming = checked(std::move(*parsed));
        auto guard = lock();
        value_ = std::move(incoming);
        changed_ = true;
    }

    [[nodiscard]] std::string default_string() const override {
        return property_codec<T>::to_string(default_);  // дефолт неизменяем — замок не нужен
    }

   private:
    // Список вариантов — единственная подсказка, по которой автор конфига
    // поймёт, что писать; собирается здесь на всех потребителей сразу
    // (конфиг, -p, studio) и пуст у неограниченных пропертей.
    [[nodiscard]] std::string options_hint() const {
        if (options().empty()) {
            return {};
        }
        std::string hint = " (expected one of: ";
        for (std::size_t i = 0; i < options().size(); ++i) {
            hint += (i == 0 ? "" : ", ") + options()[i];
        }
        return hint + ")";
    }

    // Единственная проверка вхождения в набор — через неё проходят дефолт,
    // типизированная запись и from_string. Сравнение по канонической строке:
    // так один и тот же код обслуживает и таблицу имён enum (значение вне
    // таблицы печатается пустой строкой и не совпадёт ни с чем), и набор
    // значений любого другого типа. У неограниченной проперти набор пуст —
    // ни сравнения, ни печати, ни аллокации.
    [[nodiscard]] T checked(T value) const {
        if (!options().empty()) {
            const std::string text = property_codec<T>::to_string(value);
            if (std::ranges::find(options(), text) == options().end()) {
                throw std::invalid_argument("property '" + name() + "': value '" + text + "' is not allowed" +
                                            options_hint());
            }
        }
        return value;
    }

    T default_;  // после конструктора не меняется
    T value_;
    bool changed_ = false;
};

}  // namespace atp::io

#endif  // ANITOOLSPLATFORM_IO_PROPERTY_HPP
```

- [ ] **Step 4: собрать и прогнать** `--gtest_filter="Property.*"`. Ожидание: PASS.
- [ ] **Step 5: стоп-точка** — дифф пользователю.

---

### Task 4: секция properties, третья секция ports, module/module_base, plugin_abi

**Files:**
- Create: `include/atp/io/properties.hpp`
- Modify: `include/atp/io/ports.hpp`
- Modify: `include/atp/module_base.hpp` (forward-декларация + пара виртуалов)
- Modify: `include/atp/module.hpp` (ковариантные overrides)
- Modify: `include/atp/plugin.hpp` (plugin_abi 7 → 8)
- Modify: `include/atp/io.hpp` (новые include)
- Modify: `src/runtime/include/atp/group.hpp` (группа — наследник module_base: обязана реализовать новую пару виртуалов)
- Modify: `tests/ports_tests.cpp`, `tests/module_tests.cpp` (новые тесты)

**Interfaces:**
- Consumes: `property_base`/`property<T>`/`persistence` (Task 3), вариадик `make` (Task 2).
- Produces:
  - `atp::io::properties : detail::io_registry<property_base>` (kind-слово `"property"`);
  - `atp::io::ports<TIn = inputs, TOut = outputs, TProps = properties>` с членом `props` и типом `props_type`; концепт `ports_node` учитывает три секции;
  - `atp::module_base::properties()` (пара const/non-const, чистые виртуалы) — доступ Task 6/8/9/11;
  - `module<>::properties()` ковариантно возвращает `TPorts::props_type&`.

- [ ] **Step 1: написать падающие тесты**

В `tests/ports_tests.cpp` добавить (include `<atp/io/property.hpp>` уже придёт через `<atp/io.hpp>` после Step 3; в тестах ports включён `<atp/io.hpp>` — проверить и включить при необходимости):

```cpp
namespace {

struct prop_section : atp::io::properties {
    atp::io::property<int>& limit = make<atp::io::property<int>>("limit", 10);
    atp::io::property<std::string>& file = make<atp::io::property<std::string>>("file", "", atp::io::transient);
};

}  // namespace

TEST(Ports, ThirdSectionHoldsProperties) {
    atp::io::ports<atp::io::inputs, atp::io::outputs, prop_section> node;
    EXPECT_EQ(node.props.limit.get(), 10);
    EXPECT_FALSE(node.props.file.persistent());
    // type-erased доступ через реестр — той же механикой, что порты
    EXPECT_EQ(node.props.at("limit").to_string(), "10");
}

TEST(Ports, TwoSectionFormStaysValid) {
    // обратная совместимость: прежняя форма без секции пропертей
    atp::io::ports<atp::io::inputs, atp::io::outputs> node;
    EXPECT_TRUE(node.props.list().empty());
    static_assert(atp::io::ports_node<atp::io::ports<>>);
}
```

В `tests/module_tests.cpp` добавить:

```cpp
namespace {

struct counter_props : atp::io::properties {
    atp::io::property<int>& step = make<atp::io::property<int>>("step", 1);
};
using counter_props_ports = atp::io::ports<atp::io::inputs, atp::io::outputs, counter_props>;

class propertied_module : public atp::module<counter_props_ports, "propertied"> {};

}  // namespace

TEST(Module, PropertiesCovariantAccess) {
    propertied_module m;
    EXPECT_EQ(m.properties().step.get(), 1);  // конкретный тип видит секцию
}

TEST(Module, PropertiesReachableThroughBase) {
    propertied_module m;
    atp::module_base& base = m;
    atp::io::property_base* p = base.properties().find("step");
    ASSERT_NE(p, nullptr);
    p->from_string("5");
    EXPECT_EQ(m.properties().step.get(), 5);
    EXPECT_TRUE(m.properties().step.changed());
}

TEST(Module, DefaultModuleHasEmptyProperties) {
    atp::module<> m;
    EXPECT_TRUE(m.properties().list().empty());
}
```

- [ ] **Step 2: собрать — ожидание: ошибки компиляции** (нет `properties`, нет третьей секции).

- [ ] **Step 3: реализовать**

`include/atp/io/properties.hpp`:

```cpp
#ifndef ANITOOLSPLATFORM_IO_PROPERTIES_HPP
#define ANITOOLSPLATFORM_IO_PROPERTIES_HPP

#include <atp/io/io_registry.hpp>
#include <atp/io/property_base.hpp>

namespace atp::io {

// Реестр пропертей; владеет ими. Зеркало inputs/outputs — та же механика
// и тот же паттерн объявления секции, дефолт и теги уходят конструктору:
//     property<int>& limit = make<property<int>>("limit", 10);
//     property<std::string>& file = make<property<std::string>>("file", "", transient);
//     property<int>& channels = make<property<int>>("channels", 2, allowed(1, 2, 6));
class properties : public detail::io_registry<property_base> {
   public:
    properties() : io_registry("property") {}
};

}  // namespace atp::io

#endif  // ANITOOLSPLATFORM_IO_PROPERTIES_HPP
```

`ports.hpp` — заменить шаблон и концепт (комментарий класса дополнить примером секции пропертей):

```cpp
#include <atp/io/inputs.hpp>
#include <atp/io/outputs.hpp>
#include <atp/io/properties.hpp>
...
template <std::derived_from<inputs> TIn = inputs, std::derived_from<outputs> TOut = outputs,
          std::derived_from<properties> TProps = properties>
struct ports {
    // Типы секций — для ковариантных inputs()/outputs()/properties() у module<>.
    using in_type = TIn;
    using out_type = TOut;
    using props_type = TProps;

    TIn in;
    TOut out;
    TProps props;
};

template <typename T>
concept ports_node =
    std::derived_from<T, ports<typename T::in_type, typename T::out_type, typename T::props_type>>;
```

`module_base.hpp` — к forward-декларациям добавить `class properties;`, после блока `outputs()` добавить:

```cpp
    // Проперти — третий реестр модуля: значения-настройки, редактируемые
    // на лету (builder, CLI, studio идут этим type-erased путём).
    [[nodiscard]] virtual io::properties& properties() = 0;
    [[nodiscard]] virtual const io::properties& properties() const = 0;
```

`module.hpp` — после overrides `outputs()`:

```cpp
    [[nodiscard]] TPorts::props_type& properties() override {
        return io_.props;
    }
    [[nodiscard]] const TPorts::props_type& properties() const override {
        return io_.props;
    }
```

`plugin.hpp` — `plugin_abi` 7 → 8, строка в комментарий-историю:

```cpp
// 8: проперти модулей — третья секция ports<TIn, TOut, TProps>,
//    module_base отдаёт properties() (реестр property_base).
inline constexpr unsigned plugin_abi = 8;
```

`io.hpp` — в алфавитный список include добавить `<atp/io/enum_names.hpp>`, `<atp/io/properties.hpp>`, `<atp/io/property.hpp>`, `<atp/io/property_base.hpp>`, `<atp/io/property_codec.hpp>`.

(Отдельного ABI-бампа под перечисления не нужно: `property_base::options()` и второй конструктор `property<T>` въезжают в тот же переход 7 → 8.)

- [ ] **Step 4: реализовать overrides у group** — `src/runtime/include/atp/group.hpp`: группа наследует `module_base` и без новой пары виртуалов не скомпилируется. Рядом с её overrides `inputs()`/`outputs()` добавить:

```cpp
    // Группа-композит собственных пропертей не имеет: реестр пуст.
    // Проперти детей достаются по путям (см. property_override), алиасов
    // на уровне группы нет — до появления нужды (решение спеки).
    [[nodiscard]] io::properties& properties() override {
        return properties_;
    }
    [[nodiscard]] const io::properties& properties() const override {
        return properties_;
    }
```

и приватный член `io::properties properties_;` рядом с реестрами группы (найти объявления её `inputs_`/`outputs_` и положить следом).

- [ ] **Step 5: собрать и прогнать** `--gtest_filter="Ports.*:Module.*"`, затем полный прогон (группы и раннер зависят от module_base). Ожидание: PASS.
- [ ] **Step 6: стоп-точка** — дифф пользователю.

---

### Task 5: watcher — правило для проперти

**Files:**
- Modify: `include/atp/io/watcher.hpp`
- Modify: `tests/io_tests.cpp` (Watcher-тесты живут здесь, с `tests/io_tests.cpp:302`)

**Interfaces:**
- Consumes: `property<T>::take()` (Task 3).
- Produces: перегрузка `watcher::watch(property<T>&, std::function<void(const T&)>)`.

- [ ] **Step 1: написать падающий тест** (в `tests/io_tests.cpp` к остальным Watcher-тестам):

```cpp
TEST(Watcher, PropertyRuleFiresOnChange) {
    atp::io::property<int> limit("limit", 10);
    atp::io::watcher w;
    std::vector<int> seen;
    w.watch(limit, [&](const int& v) { seen.push_back(v); });

    EXPECT_EQ(w.poll(), atp::io::work_status::idle);  // дефолт — не событие
    limit(42);
    EXPECT_EQ(w.poll(), atp::io::work_status::busy);
    EXPECT_EQ(w.poll(), atp::io::work_status::idle);  // изменение обработано ровно раз
    ASSERT_EQ(seen.size(), 1u);
    EXPECT_EQ(seen[0], 42);
}
```

- [ ] **Step 2: собрать — ожидание: ошибка компиляции** (нет перегрузки).
- [ ] **Step 3: реализовать** — в `watcher.hpp` добавить `#include <atp/io/property.hpp>`, перегрузку после queued-версии и правило после `queue_rule`:

```cpp
    // property<T>: значение изменилось (см. property::take) → обработчик.
    // Реакция на настройки декларируется рядом с правилами для входов.
    template <typename T>
    void watch(property<T>& prop, std::type_identity_t<std::function<void(const T&)>> handler) {
        rules_.push_back(std::make_unique<property_rule<T>>(prop, std::move(handler)));
    }
```

```cpp
    template <typename T>
    struct property_rule : rule_base {
        property_rule(property<T>& prop, std::function<void(const T&)> handler)
            : prop(&prop), handler(std::move(handler)) {}
        work_status poll() override {
            std::optional<T> value = prop->take();
            if (!value) {
                return work_status::idle;
            }
            handler(*value);
            return work_status::busy;
        }
        property<T>* prop;
        std::function<void(const T&)> handler;
    };
```

- [ ] **Step 4: собрать и прогнать** `--gtest_filter="Watcher.*"`. Ожидание: PASS.
- [ ] **Step 5: стоп-точка** — дифф пользователю.

---

### Task 6: конфиг — properties вместо params (модель, валидатор, builder, адаптация потребителей)

Самая связная задача: смена типа `module_node` ломает всех потребителей `params` — они приводятся в порядок здесь же, одним куском, чтобы сборка осталась зелёной. `module_config` при этом ещё жив (его удалит Task 7) — фабрики продолжают компилироваться.

**Files:**
- Modify: `src/runtime/include/atp/runtime/config_model.hpp` (module_node, schema 1.1, decode/encode)
- Modify: `src/runtime/include/atp/runtime/config_validator.hpp` (ключ properties + проверка скаляров)
- Modify: `src/runtime/include/atp/runtime/pipeline_builder.hpp` (создание без params + применение пропертей)
- Modify: `src/studio/include/atp/studio/document.hpp` (add_module без params; set_params/parse_params удалить)
- Modify: `src/studio/ui/inspector_widget.cpp:102-110` (удалить params-редактор; замена — Task 12)
- Modify: `examples/plugin_demo/counter_modules.hpp` (printer на проперти)
- Modify: `tests/runtime_config_model_tests.cpp`, `tests/runtime_config_validator_tests.cpp`, `tests/runtime_pipeline_builder_tests.cpp`, `tests/studio_document_tests.cpp:140-141`

**Interfaces:**
- Consumes: `module_base::properties()` (Task 4), `property_base::from_string` (Task 3).
- Produces:
  - `runtime::module_node.properties : std::vector<std::pair<std::string, nlohmann::json>>` (поле `params` удалено);
  - `runtime::config_schema_version{1, 1}`;
  - `runtime::detail::scalar_to_string(const nlohmann::json&) -> std::string` и `runtime::detail::apply_properties(module_base&, const module_node&)` в `pipeline_builder.hpp` — Task 8 переиспользует scalar-конвертацию идеологически, Task 10 — модель;
  - конфиг-узел: `"properties": {"имя": скаляр}`.

- [ ] **Step 1: написать падающие тесты**

`tests/runtime_config_model_tests.cpp` — заменить упоминания params (строки 19, 39, 70, 81): в JSON-фикстурах `"params": {"rate": 10}` → `"properties": {"rate": 10, "file": "a.txt", "verbose": true}`, проверки:

```cpp
    ASSERT_EQ(cfg.pipeline.children[0].module->properties.size(), 3u);
    // порядок пар — порядок items() объекта (алфавитный у nlohmann)
    EXPECT_EQ(cfg.pipeline.children[0].module->properties[0].first, "file");
    EXPECT_EQ(cfg.pipeline.children[0].module->properties[0].second, nlohmann::json("a.txt"));
    EXPECT_EQ(cfg.pipeline.children[0].module->properties[1].second, nlohmann::json(10));
    EXPECT_EQ(cfg.pipeline.children[0].module->properties[2].second, nlohmann::json(true));
```

строку 70 (`params.empty()`) → `EXPECT_TRUE(cfg.pipeline.children[0].module->properties.empty());`. Round-trip тест (encode(decode)==doc) с properties-узлом обязан сохранять типы скаляров (5, не "5").

`tests/runtime_config_validator_tests.cpp` — добавить:

```cpp
TEST(ConfigValidator, PropertiesMustBeScalarObject) {
    const nlohmann::json doc = nlohmann::json::parse(R"({
        "version": "1.1",
        "pipeline": {"children": [
            {"module": "m1", "properties": {"ok": 5, "bad": {"nested": 1}}},
            {"module": "m2", "properties": [1, 2]}
        ]}
    })");
    const auto errors = atp::runtime::validate(doc);
    ASSERT_EQ(errors.size(), 2u);
    EXPECT_NE(errors[0].find("properties.bad"), std::string::npos);
    EXPECT_NE(errors[1].find("must be an object"), std::string::npos);
}

TEST(ConfigValidator, OldParamsKeyIsRejected) {
    const nlohmann::json doc = nlohmann::json::parse(R"({
        "version": "1.1",
        "pipeline": {"children": [{"module": "m", "params": {"x": 1}}]}
    })");
    const auto errors = atp::runtime::validate(doc);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors[0].find("unknown key 'params'"), std::string::npos);
}
```

`tests/runtime_pipeline_builder_tests.cpp` — мигрировать `recording_sink` (строки 41-65): вместо `module_config` — секция пропертей:

```cpp
struct sink_props : atp::io::properties {
    atp::io::property<std::string>& tag = make<atp::io::property<std::string>>("tag");
};
// Третьим параметром ports recording_sink получает секцию пропертей; первые
// два параметра — те же, что у его текущего drain_ports (секция входов с
// queued_input "value"). Сам drain_ports остаётся для других модулей файла.
```

У класса `recording_sink`: конструктор с `module_config`, член `config_` и `static inline std::string last_params` удалить; `static inline std::latch* delivered` и `iterate` — без изменений.

В `TEST(PipelineBuilder, BuildsTreeParamsAndRuns)`: фикстура `"params": {"tag": "demo"}` → `"properties": {"tag": "demo"}`; проверку `last_params` заменить на чтение с живого модуля:

```cpp
    auto* rec = app.pipe.root().find_group("right")->find_module("recorder");
    ASSERT_NE(rec, nullptr);
    EXPECT_EQ(rec->properties().at("tag").to_string(), "demo");
```

Добавить два теста ошибок builder:

```cpp
TEST(PipelineBuilder, UnknownPropertyIsConfigError) {
    const atp::runtime::config cfg = make_config(R"({
        "version": "1.1",
        "pipeline": {"children": [{"module": "recorder", "properties": {"ghost": 1}}]}
    })");
    atp::runtime::application app;
    app.registry.add<recording_sink>();
    try {
        atp::runtime::build(app, cfg, ".");
        FAIL() << "expected config_error";
    } catch (const atp::runtime::config_error& e) {
        EXPECT_NE(std::string(e.what()).find("recorder"), std::string::npos);
        EXPECT_NE(std::string(e.what()).find("ghost"), std::string::npos);
    }
}

TEST(PipelineBuilder, UnparsableValueIsConfigError) {
    // tag у recorder — строковый, строка парсится всегда; отказ парсинга
    // демонстрирует числовая проперть limiter'а
    const atp::runtime::config cfg = make_config(R"({
        "version": "1.1",
        "pipeline": {"children": [{"module": "limiter", "properties": {"limit": "abc"}}]}
    })");
    atp::runtime::application app;
    app.registry.add<limit_sink>();
    try {
        atp::runtime::build(app, cfg, ".");
        FAIL() << "expected config_error";
    } catch (const atp::runtime::config_error& e) {
        EXPECT_NE(std::string(e.what()).find("limiter"), std::string::npos);
        EXPECT_NE(std::string(e.what()).find("abc"), std::string::npos);
    }
}
```

Рядом с `recording_sink` объявить второй тест-модуль (он же носитель enum-проперти):

```cpp
enum class overflow_policy { drop, block };

}  // namespace  — таблица имён специализируется вне анонимного namespace

template <>
struct atp::io::enum_names<overflow_policy> {
    static constexpr std::array entries{
        atp::io::enum_entry{overflow_policy::drop, "drop"},
        atp::io::enum_entry{overflow_policy::block, "block"},
    };
};

namespace {

struct limiter_props : atp::io::properties {
    atp::io::property<int>& limit = make<atp::io::property<int>>("limit");
    // перечисление на уровне типа — в конфиге имя строкой
    atp::io::property<overflow_policy>& on_overflow =
        make<atp::io::property<overflow_policy>>("on_overflow", overflow_policy::drop);
    // перечисление на уровне экземпляра — в конфиге число
    atp::io::property<int>& channels = make<atp::io::property<int>>("channels", 2, atp::io::allowed(1, 2, 6));
};
class limit_sink
    : public atp::module<atp::io::ports<atp::io::inputs, atp::io::outputs, limiter_props>, "limiter"> {};
```

И три теста пути перечислений через конфиг (кода в билдере они не требуют — значение приезжает своим JSON-типом и проверяется самой пропертью):

```cpp
TEST(PipelineBuilder, EnumPropertyComesFromConfigAsName) {
    const atp::runtime::config cfg = make_config(R"({
        "version": "1.1",
        "pipeline": {"children": [{"module": "limiter", "properties": {"on_overflow": "block"}}]}
    })");
    atp::runtime::application app;
    app.registry.add<limit_sink>();
    atp::runtime::build(app, cfg, ".");
    auto* m = app.pipe.root().find_module("limiter");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->properties().at("on_overflow").to_string(), "block");
}

TEST(PipelineBuilder, UnknownEnumNameListsOptions) {
    const atp::runtime::config cfg = make_config(R"({
        "version": "1.1",
        "pipeline": {"children": [{"module": "limiter", "properties": {"on_overflow": "explode"}}]}
    })");
    atp::runtime::application app;
    app.registry.add<limit_sink>();
    try {
        atp::runtime::build(app, cfg, ".");
        FAIL() << "expected config_error";
    } catch (const atp::runtime::config_error& e) {
        EXPECT_NE(std::string(e.what()).find("explode"), std::string::npos);
        EXPECT_NE(std::string(e.what()).find("drop, block"), std::string::npos);  // подсказка от property
    }
}

// Перечисление из чисел: значение в конфиге остаётся числом, а не строкой,
// и вне набора отвергается тем же путём, что имя вне таблицы.
TEST(PipelineBuilder, NumericOptionSetIsCheckedToo) {
    atp::runtime::application app;
    app.registry.add<limit_sink>();
    const atp::runtime::config ok = make_config(R"({
        "version": "1.1",
        "pipeline": {"children": [{"module": "limiter", "properties": {"channels": 6}}]}
    })");
    atp::runtime::build(app, ok, ".");
    EXPECT_EQ(app.pipe.root().find_module("limiter")->properties().at("channels").to_string(), "6");

    atp::runtime::application other;
    other.registry.add<limit_sink>();
    const atp::runtime::config bad = make_config(R"({
        "version": "1.1",
        "pipeline": {"children": [{"module": "limiter", "properties": {"channels": 3}}]}
    })");
    try {
        atp::runtime::build(other, bad, ".");
        FAIL() << "expected config_error";
    } catch (const atp::runtime::config_error& e) {
        EXPECT_NE(std::string(e.what()).find("1, 2, 6"), std::string::npos);
    }
}
```

`tests/studio_document_tests.cpp:140-141` — оба `set_params`-ожидания удалить (замена появится в Task 10).

- [ ] **Step 2: собрать — ожидание: ошибки компиляции** по всему фронту params.

- [ ] **Step 3: реализовать**

`config_model.hpp`:

```cpp
inline constexpr version config_schema_version{1, 1};
...
struct module_node {
    std::string factory;
    std::string name;
    std::optional<version> factory_version;
    // Начальные значения пропертей: имя → JSON-скаляр. Значение хранится
    // узлом, не строкой: encode обязан отличать 5 от "5" (round-trip).
    std::vector<std::pair<std::string, nlohmann::json>> properties;
};
```

`decode_module` — ветку params заменить:

```cpp
    if (j.contains("properties")) {
        const nlohmann::json props = j.at("properties");  // items() держит ссылку — см. про value() ниже
        for (const auto& [name, value] : props.items()) {
            m.properties.emplace_back(name, value);
        }
    }
```

`encode_child` — ветку params заменить:

```cpp
    if (!c.module->properties.empty()) {
        nlohmann::json props = nlohmann::json::object();
        for (const auto& [name, value] : c.module->properties) {
            props[name] = value;
        }
        j["properties"] = std::move(props);
    }
```

`config_validator.hpp` — в `check_child` (строка 173): `{"module", "name", "version", "params"}` → `{"module", "name", "version", "properties"}`; комментарий про params (строка 185) заменить проверкой:

```cpp
            if (node.contains("properties")) {
                const std::string ppath = path + ".properties";
                if (!node.at("properties").is_object()) {
                    error(ppath, "must be an object of name -> scalar");
                } else {
                    for (const auto& [pname, pvalue] : node.at("properties").items()) {
                        // существование проперти у модуля проверит builder — тут нужен реестр
                        if (!pvalue.is_number() && !pvalue.is_string() && !pvalue.is_boolean()) {
                            error(ppath + "." + pname, "must be a scalar (number, string or boolean)");
                        }
                    }
                }
            }
```

`pipeline_builder.hpp` — в `detail` перед `build_group`:

```cpp
// JSON-скаляр → строка для property_base::from_string: строка как есть
// (dump добавил бы кавычки), bool — словами (дефолт кодека), числа — dump
// (каноничный текст). Симметрия обратного пути — забота studio (kind).
inline std::string scalar_to_string(const nlohmann::json& value) {
    if (value.is_string()) {
        return value.get<std::string>();
    }
    if (value.is_boolean()) {
        return value.get<bool>() ? "true" : "false";
    }
    return value.dump();
}

// Значения из конфига — до g.add и initialize: модуль в initialize уже
// видит настройки. Неизвестное имя и непарсящееся значение — ошибки
// конфига; контекст модуля добавит вызывающий. invalid_argument проперти
// переводится в config_error как есть: её текст уже называет проперть и,
// у enum, перечисляет допустимые имена.
inline void apply_properties(module_base& m, const module_node& node) {
    for (const auto& [name, value] : node.properties) {
        io::property_base* prop = m.properties().find(name);
        if (prop == nullptr) {
            throw config_error("no property named '" + name + "'");
        }
        try {
            prop->from_string(scalar_to_string(value));
        } catch (const std::invalid_argument& e) {
            throw config_error(e.what());
        }
    }
}
```

В `build_group` заменить создание (обе ветки create теряют params):

```cpp
                module_ptr m = c.module->factory_version
                                   ? registry.create(c.module->factory, *c.module->factory_version)
                                   : registry.create(c.module->factory);
                apply_properties(*m, *c.module);
                g.add(c.module->name, std::move(m));
```

(create-перегрузки со строкой конфига пока существуют — Task 7 удалит их; здесь просто перестаём звать.)

`document.hpp`: у `add_module` удалить параметр `std::string params = {}` и связанные строки 183-185, 188 → `c.module = runtime::module_node{factory, std::move(name), factory_version, {}};` метод `set_params` (строки 285-294) и `parse_params` (строки 451-458) удалить целиком.

`inspector_widget.cpp:102-110`: удалить блок QPlainTextEdit/«Apply params» (лямбда-захват `params` тоже уходит).

`examples/plugin_demo/counter_modules.hpp`: printer мигрирует:

```cpp
struct printer_inputs : atp::io::inputs {
    atp::io::queued_input<int>& value = make<atp::io::queued_input<int>>("value");
};
struct printer_props : atp::io::properties {
    // tag печатается перед каждым значением; настраивается из конфига/-p/studio
    atp::io::property<std::string>& tag = make<atp::io::property<std::string>>("tag");
};
using printer_ports = atp::io::ports<printer_inputs, atp::io::outputs, printer_props>;

class printer_module : public atp::module<printer_ports, "printer", atp::ver<"1.0">> {
   public:
    void start() override {
        const std::string tag = properties().tag.get();
        if (!tag.empty()) {
            std::cout << "printer tag: " << tag << std::endl;
        }
    }
    atp::work_status iterate(std::stop_token) override { /* без изменений */ }
};
```

Конструктор с `module_config`, член `config_` и `#include <atp/module_config.hpp>` удалить.

- [ ] **Step 4: собрать всё и прогнать полный набор.** Ожидание: PASS (в т.ч. studio-тесты).
- [ ] **Step 5: стоп-точка** — дифф пользователю (задача крупная — перечислить файлы явно).

---

### Task 7: удаление module_config

**Files:**
- Delete: `include/atp/module_config.hpp`
- Modify: `include/atp/module_factory_base.hpp` (create() — единственный чистый виртуал)
- Modify: `include/atp/module_factory.hpp` (без конфигной ветки)
- Modify: `include/atp/module_registry.hpp` (create/концепты без module_config; pinned_factory)
- Modify: `tests/module_factory_tests.cpp` (params-модули и конфигные тесты удалить)

**Interfaces:**
- Produces: `module_factory_base::create() -> module_ptr` — чистый виртуал без параметров (прежний `create(const std::string&)` и сахар удалены); `module_factory<M, TArgs...>` требует только `std::constructible_from<M, const TArgs&...>`; `module_registry::create(name)`/`create(name, version)` — единственные перегрузки.

- [ ] **Step 1: обновить тесты (падающими они станут после Step 2 — здесь метка среза):** в `tests/module_factory_tests.cpp` удалить `params_module`, `params_with_args_module`, `named_plain_module`, include `<atp/module_config.hpp>` и тесты `PassesConfigStringToModule`, `ConfigComesFirstBoundArgsAfter`, `ParameterlessModuleRejectsNonEmptyConfig`. Взамен один тест, что фабрика со связанными аргументами жива:

```cpp
TEST(ModuleFactory, BoundArgsReachEveryInstance) {
    atp::module_factory<configured_module, int> factory("configured", 42);  // configured_module в файле уже есть
    EXPECT_EQ(dynamic_cast<configured_module&>(*factory.create()).value(), 42);
}
```

- [ ] **Step 2: удалить механизм**
  - `module_factory_base.hpp`: `create(const std::string&)` и сахар `create()` заменить одним `[[nodiscard]] virtual module_ptr create() const = 0;` комментарий про params-строку убрать.
  - `module_factory.hpp`: include `module_config.hpp` убрать; requires-клаузу класса сократить до `std::constructible_from<M, const TArgs&...>`; `using module_factory_base::create;` убрать; `create(const std::string&)` заменить на:

```cpp
    [[nodiscard]] module_ptr create() const override {
        return std::apply([](const TArgs&... args) { return module_ptr(new M(args...), {}); }, args_);
    }
```

  - `module_registry.hpp`: перегрузки `create(name, config)` / `create(name, v, config)` (строки 92-97) удалить; в обоих концептах `add()` (registry и registrar) убрать альтернативу `std::constructible_from<M, const module_config&, ...>`; в `pinned_factory` — `using ...::create` убрать, override переписать:

```cpp
    [[nodiscard]] module_ptr create() const override {
        module_ptr m = inner_->create();
        return module_ptr(m.release(), module_deleter{pin_});
    }
```

  - Удалить файл `include/atp/module_config.hpp`.
- [ ] **Step 3: `grep -r module_config` по репозиторию** — ожидание: упоминания только в `docs/architecture.md` и `.claude/CLAUDE.md` (их обновит Task 13).
- [ ] **Step 4: собрать всё и прогнать полный набор.** Ожидание: PASS.
- [ ] **Step 5: стоп-точка** — дифф пользователю.

---

### Task 8: property_override — разбор и применение; atp_app -p

**Files:**
- Create: `src/runtime/include/atp/runtime/property_override.hpp`
- Create: `tests/runtime_property_override_tests.cpp` (+ строка в `tests/CMakeLists.txt`)
- Modify: `src/app/main.cpp`

**Interfaces:**
- Consumes: `group::find_group/find_module` (существующие), `module_base::properties()`, `config_error`.
- Produces (namespace `atp::runtime`):
  - `struct property_override { std::string module_path; std::string name; std::string value; };`
  - `parse_property_override(std::string_view arg) -> property_override` — бросает `config_error`;
  - `find_property(group& root, std::string_view module_path, const std::string& name) -> io::property_base&` — спуск по дереву, бросает `config_error` (нет группы/модуля/проперти);
  - `apply_property_override(group& root, const property_override& o)` — `find_property` + `from_string`, бросает `config_error`.
  - Task 11 (session) применяет правки через apply; Task 12 (инспектор) читает живые значения через find_property.

- [ ] **Step 1: написать падающие тесты**

`tests/runtime_property_override_tests.cpp`:

```cpp
#include <string>

#include <gtest/gtest.h>

#include <atp/group.hpp>
#include <atp/module.hpp>
#include <atp/runtime/property_override.hpp>

namespace {

enum class trace_level { off, brief, full };

}  // namespace

template <>
struct atp::io::enum_names<trace_level> {
    static constexpr std::array entries{
        atp::io::enum_entry{trace_level::off, "off"},
        atp::io::enum_entry{trace_level::brief, "brief"},
        atp::io::enum_entry{trace_level::full, "full"},
    };
};

namespace {

struct target_props : atp::io::properties {
    atp::io::property<int>& limit = make<atp::io::property<int>>("limit", 10);
    atp::io::property<trace_level>& trace = make<atp::io::property<trace_level>>("trace");
};
class target_module : public atp::module<atp::io::ports<atp::io::inputs, atp::io::outputs, target_props>, "target"> {};

TEST(PropertyOverride, ParsesPathNameValue) {
    const auto o = atp::runtime::parse_property_override("grp.mod.limit=5");
    EXPECT_EQ(o.module_path, "grp.mod");
    EXPECT_EQ(o.name, "limit");
    EXPECT_EQ(o.value, "5");
}

TEST(PropertyOverride, ValueMayContainEqualsAndDots) {
    const auto o = atp::runtime::parse_property_override("mod.file=C:\\a=b\\x.txt");
    EXPECT_EQ(o.module_path, "mod");
    EXPECT_EQ(o.name, "file");
    EXPECT_EQ(o.value, "C:\\a=b\\x.txt");  // режем по ПЕРВОМУ '='; точки справа — часть значения
}

TEST(PropertyOverride, MalformedStringsThrow) {
    EXPECT_THROW((void)atp::runtime::parse_property_override("no-equals"), atp::runtime::config_error);
    EXPECT_THROW((void)atp::runtime::parse_property_override("nodots=5"), atp::runtime::config_error);
    EXPECT_THROW((void)atp::runtime::parse_property_override("=5"), atp::runtime::config_error);
    EXPECT_THROW((void)atp::runtime::parse_property_override("mod.=5"), atp::runtime::config_error);
    EXPECT_THROW((void)atp::runtime::parse_property_override(".limit=5"), atp::runtime::config_error);
}

TEST(PropertyOverride, AppliesThroughGroupTree) {
    atp::group root("root");
    atp::group& sub = root.add_group("sub");
    auto* m = new target_module();
    sub.add("mod", atp::module_ptr(m, {}));

    atp::runtime::apply_property_override(root, {"sub.mod", "limit", "42"});
    EXPECT_EQ(m->properties().limit.get(), 42);
}

// Enum отдельного кода не требует — имя приезжает строкой и парсится самой
// пропертью; тест фиксирует, что и подсказка из сообщения доезжает до -p.
TEST(PropertyOverride, EnumValueIsTakenByName) {
    atp::group root("root");
    auto* m = new target_module();
    root.add("mod", atp::module_ptr(m, {}));

    atp::runtime::apply_property_override(root, {"mod", "trace", "full"});
    EXPECT_EQ(m->properties().trace.get(), trace_level::full);
    try {
        atp::runtime::apply_property_override(root, {"mod", "trace", "verbose"});
        FAIL() << "expected config_error";
    } catch (const atp::runtime::config_error& e) {
        EXPECT_NE(std::string(e.what()).find("off, brief, full"), std::string::npos);
    }
}

TEST(PropertyOverride, BadPathsAndValuesAreConfigErrors) {
    atp::group root("root");
    auto* m = new target_module();
    root.add("mod", atp::module_ptr(m, {}));

    EXPECT_THROW(atp::runtime::apply_property_override(root, {"ghost.mod", "limit", "1"}),
                 atp::runtime::config_error);  // нет группы
    EXPECT_THROW(atp::runtime::apply_property_override(root, {"ghost", "limit", "1"}),
                 atp::runtime::config_error);  // нет модуля
    EXPECT_THROW(atp::runtime::apply_property_override(root, {"mod", "ghost", "1"}),
                 atp::runtime::config_error);  // нет проперти
    EXPECT_THROW(atp::runtime::apply_property_override(root, {"mod", "limit", "abc"}),
                 atp::runtime::config_error);  // не парсится — invalid_argument завёрнут
}

}  // namespace
```

Примечание: сигнатуру конструктора `group`/`add_group`/`add` сверить с `src/runtime/include/atp/group.hpp` по месту (add — `src/runtime/include/atp/group.hpp:50`); если `group` строится иначе (например, через `pipeline().root()`), использовать `atp::pipeline pipe; pipe.root()...` — контракт теста от этого не меняется.

- [ ] **Step 2: собрать — ожидание: ошибка компиляции.**

- [ ] **Step 3: реализовать** `src/runtime/include/atp/runtime/property_override.hpp`:

```cpp
#ifndef ATP_RUNTIME_PROPERTY_OVERRIDE_HPP
#define ATP_RUNTIME_PROPERTY_OVERRIDE_HPP

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

#include <atp/group.hpp>
#include <atp/runtime/config_model.hpp>

namespace atp::runtime {

// Правка одной проперти по пути в дереве групп: источник — флаг -p у
// atp_app ("group.module.prop=value") и правки на лету из studio.
struct property_override {
    std::string module_path;  // путь модуля в дереве групп, сегменты через '.'
    std::string name;         // имя проперти
    std::string value;        // строковое значение (парсит сама проперть)
};

// Разбор "path.prop=value": по ПЕРВОМУ '=' (значение может содержать '='),
// слева — по ПОСЛЕДНЕЙ '.' (имя проперти точку содержать не может, путь —
// может). Ошибки формата — config_error с исходной строкой.
[[nodiscard]] inline property_override parse_property_override(std::string_view arg) {
    const std::size_t eq = arg.find('=');
    if (eq == std::string_view::npos) {
        throw config_error("property override '" + std::string(arg) + "': expected 'path.prop=value'");
    }
    const std::string_view left = arg.substr(0, eq);
    const std::size_t dot = left.rfind('.');
    if (dot == std::string_view::npos || dot == 0 || dot + 1 == left.size()) {
        throw config_error("property override '" + std::string(arg) + "': expected 'path.prop=value'");
    }
    return {std::string(left.substr(0, dot)), std::string(left.substr(dot + 1)), std::string(arg.substr(eq + 1))};
}

// Спуск по дереву: сегменты до последнего — группы, последний — модуль.
// Все отказы — config_error с полным путём: пользователь видит, что именно
// не нашлось. Переиспользуется и записью (apply), и чтением (инспектор).
[[nodiscard]] inline io::property_base& find_property(group& root,
                                                      std::string_view module_path,
                                                      const std::string& name) {
    group* current = &root;
    std::size_t begin = 0;
    while (true) {
        const std::size_t dot = module_path.find('.', begin);
        if (dot == std::string_view::npos) {
            break;
        }
        const std::string segment(module_path.substr(begin, dot - begin));
        group* next = current->find_group(segment);
        if (next == nullptr) {
            throw config_error("property override: no group '" + segment + "' in path '" +
                               std::string(module_path) + "'");
        }
        current = next;
        begin = dot + 1;
    }
    const std::string module_name(module_path.substr(begin));
    module_base* m = current->find_module(module_name);
    if (m == nullptr) {
        throw config_error("property override: no module at path '" + std::string(module_path) + "'");
    }
    io::property_base* prop = m->properties().find(name);
    if (prop == nullptr) {
        throw config_error("property override: module '" + std::string(module_path) + "' has no property '" +
                           name + "'");
    }
    return *prop;
}

// Непарсящееся значение — тоже ошибка конфигурации, не логики.
inline void apply_property_override(group& root, const property_override& o) {
    io::property_base& prop = find_property(root, o.module_path, o.name);
    try {
        prop.from_string(o.value);
    } catch (const std::invalid_argument& e) {
        throw config_error(std::string("property override: ") + e.what());
    }
}

}  // namespace atp::runtime

#endif  // ATP_RUNTIME_PROPERTY_OVERRIDE_HPP
```

- [ ] **Step 4: собрать и прогнать** `--gtest_filter="PropertyOverride.*"`. Ожидание: PASS.

- [ ] **Step 5: подключить -p в atp_app** (`src/app/main.cpp`): include `<atp/runtime/property_override.hpp>`; разбор argv вместо жёсткого `argc != 2`:

```cpp
    std::filesystem::path config_path;
    std::vector<atp::runtime::property_override> overrides;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "-p") {
            if (i + 1 == argc) {
                std::cerr << "usage: atp_app <config.json> [-p path.prop=value]...\n";
                return 2;
            }
            overrides.push_back(atp::runtime::parse_property_override(argv[++i]));
        } else if (config_path.empty()) {
            config_path = argv[i];
        } else {
            std::cerr << "usage: atp_app <config.json> [-p path.prop=value]...\n";
            return 2;
        }
    }
    if (config_path.empty()) {
        std::cerr << "usage: atp_app <config.json> [-p path.prop=value]...\n";
        return 2;
    }
```

(разбор — внутри try: config_error формата — тот же путь «сообщение + код 1»? Нет: невалидный ввод пользователя — код 2, как невалидный конфиг. Обернуть parse в отдельный try до основного и вернуть 2.) После `build(...)`, до `runner.start`:

```cpp
        // Overrides поверх конфига — до start: initialize зовётся из
        // runner.start, модуль видит значения уже в initialize.
        for (const atp::runtime::property_override& o : overrides) {
            atp::runtime::apply_property_override(app.pipe.root(), o);
        }
```

(`apply` бросает `config_error` — основной catch печатает и возвращает 1; для единообразия с невалидным конфигом обернуть цикл в try/catch с выводом и `return 2`.)

- [ ] **Step 6: собрать всё**; ручная проверка не требуется (main тонкий, хелпер покрыт тестами). Полный прогон — PASS.
- [ ] **Step 7: стоп-точка** — дифф пользователю.

---

### Task 9: studio describe — проперти в module_info

**Files:**
- Modify: `src/studio/include/atp/studio/module_manager.hpp` (property_info, module_info, describe)
- Modify: `tests/studio_module_manager_tests.cpp`

**Interfaces:**
- Consumes: `property_base::{kind, options, persistent, default_string}` (Task 3), `properties().owned()` (Task 4).
- Produces: `atp::studio::property_info { std::string name; io::property_kind kind; std::string default_value; std::vector<std::string> options; bool persistent; }`; поле `std::vector<property_info> properties;` в `module_info` (после `outputs`). Task 12 строит по нему виджеты: непустой `options` → выпадающий список, `kind` → как разбирать введённое обратно в JSON. Набор копируется — описание переживает временный модуль-зонд, с которого снято.

- [ ] **Step 1: написать падающий тест** — в `tests/studio_module_manager_tests.cpp` рядом с существующими describe-тестами (найти `TEST(...describe...)` / `module_manager::describe` в файле и повторить их стиль объявления тест-модуля):

```cpp
namespace {
enum class fit { none, cover, contain };
}  // namespace

template <>
struct atp::io::enum_names<fit> {
    static constexpr std::array entries{
        atp::io::enum_entry{fit::none, "none"},
        atp::io::enum_entry{fit::cover, "cover"},
        atp::io::enum_entry{fit::contain, "contain"},
    };
};

namespace {
struct probe_props : atp::io::properties {
    atp::io::property<int>& limit = make<atp::io::property<int>>("limit", 10);
    atp::io::property<std::string>& tmp = make<atp::io::property<std::string>>("tmp", "", atp::io::transient);
    atp::io::property<fit>& scaling = make<atp::io::property<fit>>("scaling", fit::cover);
    // перечисление без enum-типа — в описании неотличимо от предыдущего
    atp::io::property<int>& channels = make<atp::io::property<int>>("channels", 2, atp::io::allowed(1, 2, 6));
};
class propertied_probe
    : public atp::module<atp::io::ports<atp::io::inputs, atp::io::outputs, probe_props>, "propertied_probe"> {};
}  // namespace

TEST(StudioModuleManager, DescribeListsPropertyOptions) {
    atp::module_factory<propertied_probe> factory("propertied_probe");
    const atp::studio::module_info info = atp::studio::module_manager::describe(factory);
    const auto by_name = [&info](std::string_view name) {
        return std::ranges::find_if(info.properties, [name](const auto& p) { return p.name == name; });
    };

    // Перечисление из таблицы имён типа: kind текстовый, набор — имена.
    const auto scaling = by_name("scaling");
    ASSERT_NE(scaling, info.properties.end());
    EXPECT_EQ(scaling->kind, atp::io::property_kind::text);
    EXPECT_EQ(scaling->default_value, "cover");
    EXPECT_EQ(scaling->options, (std::vector<std::string>{"none", "cover", "contain"}));

    // Перечисление из набора значений: описание такое же, но kind числовой —
    // инспектор нарисует список, а в конфиг вернёт число, не строку.
    const auto channels = by_name("channels");
    ASSERT_NE(channels, info.properties.end());
    EXPECT_EQ(channels->kind, atp::io::property_kind::number);
    EXPECT_EQ(channels->options, (std::vector<std::string>{"1", "2", "6"}));

    // У неограниченной проперти набор пуст — по нему и различаются виджеты.
    EXPECT_TRUE(by_name("limit")->options.empty());
}

TEST(StudioModuleManager, DescribeListsProperties) {
    atp::module_factory<propertied_probe> factory("propertied_probe");
    const atp::studio::module_info info = atp::studio::module_manager::describe(factory);
    ASSERT_EQ(info.properties.size(), 4u);
    // owned() не гарантирует порядок — найти по имени
    const auto limit = std::ranges::find_if(info.properties, [](const auto& p) { return p.name == "limit"; });
    ASSERT_NE(limit, info.properties.end());
    EXPECT_EQ(limit->kind, atp::io::property_kind::number);
    EXPECT_EQ(limit->default_value, "10");
    EXPECT_TRUE(limit->persistent);
    const auto tmp = std::ranges::find_if(info.properties, [](const auto& p) { return p.name == "tmp"; });
    ASSERT_NE(tmp, info.properties.end());
    EXPECT_FALSE(tmp->persistent);
}
```

- [ ] **Step 2: собрать — ожидание: ошибка компиляции** (нет property_info).
- [ ] **Step 3: реализовать** — в `module_manager.hpp` после `port_info`:

```cpp
struct property_info {
    std::string name;
    io::property_kind kind;                 // подсказка виджету инспектора
    std::string default_value;              // дефолт строкой — сравнение при сохранении
    std::vector<std::string> options;       // непусто у enum: элементы выпадающего списка
    bool persistent = true;
};
```

в `module_info` после `outputs`: `std::vector<property_info> properties;` — агрегатная инициализация в `describe` (module_manager.hpp:110) получает лишний `{}`:

```cpp
        module_info info{std::string(factory.name()), factory.get_version(), {}, {}, {}, false, {}};
```

в `describe` после цикла по outputs:

```cpp
            for (io::property_base* p : probe->properties().owned()) {
                info.properties.push_back(
                    {p->name(), p->kind(), p->default_string(), p->options(), p->persistent()});
            }
```

- [ ] **Step 4: собрать и прогнать** `--gtest_filter="StudioModuleManager.*"`. Ожидание: PASS.
- [ ] **Step 5: стоп-точка** — дифф пользователю.

---

### Task 10: document — set_property/clear_property

**Files:**
- Modify: `src/studio/include/atp/studio/document.hpp`
- Modify: `tests/studio_document_tests.cpp`

**Interfaces:**
- Consumes: `module_node.properties` (Task 6).
- Produces (методы `atp::studio::document`):
  - `set_property(const std::string& group_path, const std::string& name, const std::string& prop, nlohmann::json value)` — value обязан быть скаляром; заменяет пару или добавляет новую; snapshot в undo;
  - `clear_property(const std::string& group_path, const std::string& name, const std::string& prop)` — убирает пару (значение вернулось к дефолту); отсутствие пары — не ошибка, no-op без снапшота.
  - Task 12 зовёт их из инспектора.

- [ ] **Step 1: написать падающие тесты** — в `tests/studio_document_tests.cpp` (по стилю соседей; фикстуры файла смотреть на месте):

```cpp
TEST(StudioDocument, SetPropertyAddsAndReplaces) {
    auto doc = atp::studio::document::create();
    doc.add_module("", "counter");
    doc.set_property("", "counter", "limit", 5);
    ASSERT_EQ(doc.config().pipeline.children[0].module->properties.size(), 1u);
    EXPECT_EQ(doc.config().pipeline.children[0].module->properties[0].second, nlohmann::json(5));
    doc.set_property("", "counter", "limit", 7);  // замена, не дубль
    ASSERT_EQ(doc.config().pipeline.children[0].module->properties.size(), 1u);
    EXPECT_EQ(doc.config().pipeline.children[0].module->properties[0].second, nlohmann::json(7));
    EXPECT_TRUE(doc.can_undo());
}

TEST(StudioDocument, SetPropertyRejectsNonScalarAndGhostModule) {
    auto doc = atp::studio::document::create();
    doc.add_module("", "counter");
    EXPECT_THROW(doc.set_property("", "counter", "limit", nlohmann::json::object()),
                 atp::runtime::config_error);
    EXPECT_THROW(doc.set_property("", "ghost", "limit", 5), atp::runtime::config_error);
}

TEST(StudioDocument, ClearPropertyRemovesPair) {
    auto doc = atp::studio::document::create();
    doc.add_module("", "counter");
    doc.set_property("", "counter", "limit", 5);
    doc.clear_property("", "counter", "limit");
    EXPECT_TRUE(doc.config().pipeline.children[0].module->properties.empty());
    doc.clear_property("", "counter", "ghost");  // отсутствие пары — no-op, не ошибка
}
```

- [ ] **Step 2: собрать — ожидание: ошибка компиляции.**
- [ ] **Step 3: реализовать** — в `document.hpp` на месте прежнего `set_params`:

```cpp
    // Значение проперти в документе: скаляр по контракту валидатора.
    // Правка на живом пайплайне — отдельный канал (session), документ —
    // источник для сохранения и следующего запуска.
    void set_property(const std::string& group_path,
                      const std::string& name,
                      const std::string& prop,
                      nlohmann::json value) {
        if (!value.is_number() && !value.is_string() && !value.is_boolean()) {
            throw runtime::config_error("property '" + prop + "' must be a scalar (number, string or boolean)");
        }
        runtime::module_node& m = require_module(group_path, name);
        snapshot();
        for (auto& [existing, v] : m.properties) {
            if (existing == prop) {
                v = std::move(value);
                return;
            }
        }
        m.properties.emplace_back(prop, std::move(value));
    }

    // Снятие значения — «вернуться к дефолту модуля»; отсутствие пары не
    // ошибка (кнопка reset идемпотентна), снапшот только при изменении.
    void clear_property(const std::string& group_path, const std::string& name, const std::string& prop) {
        runtime::module_node& m = require_module(group_path, name);
        const auto it = std::ranges::find_if(m.properties, [&](const auto& p) { return p.first == prop; });
        if (it == m.properties.end()) {
            return;
        }
        snapshot();
        m.properties.erase(it);
    }
```

и приватный хелпер рядом с `require_group`:

```cpp
    [[nodiscard]] runtime::module_node& require_module(const std::string& group_path, const std::string& name) {
        runtime::group_node& g = require_group(group_path);
        runtime::child_node* child = detail::find_child(g, name);
        if (child == nullptr || !child->module) {
            throw runtime::config_error("no module '" + name + "' in group '" + group_path + "'");
        }
        return *child->module;
    }
```

- [ ] **Step 4: собрать и прогнать** `--gtest_filter="StudioDocument.*"`. Ожидание: PASS.
- [ ] **Step 5: стоп-точка** — дифф пользователю.

---

### Task 11: session — правка живой проперти и стягивание в документ

**Files:**
- Modify: `src/studio/include/atp/studio/session.hpp`
- Create: `src/studio/include/atp/studio/property_sync.hpp`
- Create: `tests/studio_property_sync_tests.cpp` (+ строка в `tests/CMakeLists.txt`)
- Modify: `tests/studio_session_tests.cpp`

**Interfaces:**
- Consumes: `property_override`/`apply_property_override` (Task 8), `document::set_property/clear_property` (Task 10), `module_info` не нужен (идём по живым модулям).
- Produces:
  - `session::set_property(const runtime::property_override& o)` — бросает `std::logic_error`, если не запущено; иначе применяет к живому дереву;
  - `session::live_root() -> group*` (nullptr, если не запущено) — доступ Task 12 и sync;
  - `atp::studio::sync_persistent_properties(document& doc, const runtime::config& cfg, const group& root)` в `property_sync.hpp`: обход дерева конфига, для каждого модуля — все owned persistent-проперти живого экземпляра: значение ≠ default_string() → `doc.set_property` (типизировано по kind), равно дефолту → `doc.clear_property`.

- [ ] **Step 1: написать падающие тесты**

В `tests/studio_session_tests.cpp` (стиль запуска сессии взять у соседних тестов файла — там уже есть регистрация тест-модуля и конфиг):

```cpp
TEST(StudioSession, SetPropertyReachesLiveModule) {
    // модуль с property<int> "limit" (объявить в анонимном namespace файла
    // по образцу Task 8: target_module с target_props), реестр, конфиг с ним
    atp::module_registry registry;
    registry.add<target_module>();  // "target"
    atp::studio::session s(registry);
    EXPECT_THROW(s.set_property({"target", "limit", "5"}), std::logic_error);  // не запущено
    s.start(make_config(R"({"version": "1.1", "pipeline": {"children": [{"module": "target"}]}})"));
    s.set_property({"target", "limit", "42"});
    auto* m = s.live_root()->find_module("target");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->properties().at("limit").to_string(), "42");
    s.stop();
    EXPECT_EQ(s.live_root(), nullptr) << "после stop живого дерева нет";
}
```

(`make_config` — локальный хелпер как в `runtime_pipeline_builder_tests.cpp:67`; если в session-тестах уже есть аналог — использовать его. Примечание: `live_root()` после stop — см. Step 3: pipe_ живёт до следующего start, поэтому контракт «nullptr после stop» реализуется проверкой `running()`.)

`tests/studio_property_sync_tests.cpp`:

```cpp
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <atp/module.hpp>
#include <atp/module_registry.hpp>
#include <atp/runtime/config_validator.hpp>
#include <atp/runtime/pipeline_builder.hpp>
#include <atp/studio/document.hpp>
#include <atp/studio/property_sync.hpp>

namespace {

struct sync_props : atp::io::properties {
    atp::io::property<int>& limit = make<atp::io::property<int>>("limit", 10);
    atp::io::property<bool>& verbose = make<atp::io::property<bool>>("verbose", false);
    atp::io::property<std::string>& scratch = make<atp::io::property<std::string>>("scratch", "", atp::io::transient);
};
class sync_module : public atp::module<atp::io::ports<atp::io::inputs, atp::io::outputs, sync_props>, "syncer"> {};

TEST(StudioPropertySync, PullsPersistentValuesIntoDocument) {
    atp::module_registry registry;
    registry.add<sync_module>();
    auto doc = atp::studio::document::create();
    doc.add_module("", "syncer");
    doc.set_property("", "syncer", "limit", 999);  // устареет после правок на лету

    atp::pipeline pipe;
    atp::pipeline_runner runner;
    atp::runtime::build_pipeline(pipe, runner, doc.config(), registry);

    auto* m = pipe.root().find_module("syncer");
    m->properties().at("limit").from_string("42");     // правка на лету
    m->properties().at("verbose").from_string("true");
    m->properties().at("scratch").from_string("tmp");  // transient — в документ не идёт

    atp::studio::sync_persistent_properties(doc, doc.config(), pipe.root());

    const auto& props = doc.config().pipeline.children[0].module->properties;
    ASSERT_EQ(props.size(), 2u);
    // число осталось числом, bool — булем: типы восстановлены по kind
    EXPECT_EQ(props[0].second, nlohmann::json(42));
    EXPECT_EQ(props[1].second, nlohmann::json(true));
}

TEST(StudioPropertySync, DefaultValuesAreDroppedFromDocument) {
    atp::module_registry registry;
    registry.add<sync_module>();
    auto doc = atp::studio::document::create();
    doc.add_module("", "syncer");
    doc.set_property("", "syncer", "limit", 42);

    atp::pipeline pipe;
    atp::pipeline_runner runner;
    atp::runtime::build_pipeline(pipe, runner, doc.config(), registry);
    pipe.root().find_module("syncer")->properties().at("limit").from_string("10");  // вернули дефолт

    atp::studio::sync_persistent_properties(doc, doc.config(), pipe.root());
    EXPECT_TRUE(doc.config().pipeline.children[0].module->properties.empty());
}

}  // namespace
```

- [ ] **Step 2: собрать — ожидание: ошибки компиляции.**

- [ ] **Step 3: реализовать**

`session.hpp` — include `<atp/runtime/property_override.hpp>`; публичные методы после `stop()`:

```cpp
    // Правка проперти живого модуля — канал studio «на лету». Документ она
    // не трогает: persistent-правки инспектор дублирует в document сам.
    void set_property(const runtime::property_override& o) {
        if (!running()) {
            throw std::logic_error("session is not running");
        }
        runtime::apply_property_override(pipe_->root(), o);
    }

    // Живое дерево (nullptr — не запущено): инспектору для чтения текущих
    // значений и sync_persistent_properties при сохранении на ходу.
    [[nodiscard]] group* live_root() const {
        return running() ? &pipe_->root() : nullptr;
    }
```

`property_sync.hpp`:

```cpp
#ifndef ATP_STUDIO_PROPERTY_SYNC_HPP
#define ATP_STUDIO_PROPERTY_SYNC_HPP

#include <string>

#include <nlohmann/json.hpp>

#include <atp/group.hpp>
#include <atp/runtime/config_model.hpp>
#include <atp/studio/document.hpp>

namespace atp::studio {

namespace detail {

// Строка проперти → JSON-скаляр по kind: числа и bool возвращаются в
// конфиг своим типом, не строкой (обратная сторона scalar_to_string
// builder'а). to_string кодека канонична — parse не откажет; text — как есть.
// Ограниченность набором на тип записи не влияет: перечисление из чисел
// остаётся числом, из имён enum — строкой.
[[nodiscard]] inline nlohmann::json property_value_to_json(const io::property_base& p) {
    switch (p.kind()) {
        case io::property_kind::number:
            return nlohmann::json::parse(p.to_string());
        case io::property_kind::boolean:
            return nlohmann::json(p.to_string() == "true");
        case io::property_kind::text:
            break;
    }
    return nlohmann::json(p.to_string());
}

inline void sync_group(document& doc,
                       const runtime::group_node& node,
                       const group& live,
                       const std::string& group_path) {
    for (const runtime::child_node& c : node.children) {
        if (c.module) {
            const module_base* m = live.find_module(c.module->name);
            if (m == nullptr) {
                continue;  // рассинхрон документа и запуска — не повод падать при сохранении
            }
            for (const io::property_base* p : m->properties().owned()) {
                if (!p->persistent()) {
                    continue;  // transient в документ не попадает никогда
                }
                if (p->to_string() == p->default_string()) {
                    doc.clear_property(group_path, c.module->name, p->name());
                } else {
                    doc.set_property(group_path, c.module->name, p->name(), property_value_to_json(*p));
                }
            }
        } else {
            const group* sub = live.find_group(c.group->name);
            if (sub != nullptr) {
                sync_group(doc, *c.group, *sub,
                           group_path.empty() ? c.group->name : group_path + "." + c.group->name);
            }
        }
    }
}

}  // namespace detail

// Перед сохранением на ходу: значения persistent-пропертей живых модулей —
// в документ (модуль мог поменять их и сам). Равное дефолту — из документа
// вон: конфиг не обрастает шумом.
inline void sync_persistent_properties(document& doc, const runtime::config& cfg, const group& root) {
    detail::sync_group(doc, cfg.pipeline, root, "");
}

}  // namespace atp::studio

#endif  // ATP_STUDIO_PROPERTY_SYNC_HPP
```

Примечание реализации: `properties().owned()` возвращает неконстантные указатели — если const-обход не сложится по const-корректности (`find_module` у const group), взять неконстантные `group&`/`module_base*`: сигнатуру `sync_persistent_properties(document&, const runtime::config&, group& root)` план допускает (тесты зовут с неконстантным root). Snapshot-шум: каждая set_property пишет undo-снапшот — для sync допустимо (операция редкая, перед сохранением).

- [ ] **Step 4: собрать и прогнать** `--gtest_filter="StudioSession.*:StudioPropertySync.*"`. Ожидание: PASS.
- [ ] **Step 5: стоп-точка** — дифф пользователю.

---

### Task 12: инспектор — редактор пропертей + сохранение на ходу (GUI, без юнит-тестов)

**Files:**
- Modify: `src/studio/ui/inspector_widget.hpp` (`build_property_rows` в приватные методы)
- Modify: `src/studio/ui/inspector_widget.cpp` (`build_module_section`, `refresh`)
- Modify: `src/studio/ui/main_window.cpp:60-72` (`refresh_all`) и `:170-190` (`save`)

**Interfaces:**
- Consumes: `describe_cached` → `module_info.properties` (Task 9), `document::set_property/clear_property` (Task 10), `session::set_property/live_root` + `sync_persistent_properties` (Task 11), `runtime::property_override` (Task 8).

- [ ] **Step 1: строки пропертей в build_module_section** (`inspector_widget.cpp`, вместо удалённого params-блока) — вызвать новый метод `build_property_rows(m)`; сам метод:

```cpp
void inspector_widget::build_property_rows(const runtime::module_node& m) {
    const module_info* info = state_.describe_cached(m.factory, m.factory_version);
    if (info == nullptr || info->properties.empty()) {
        return;
    }
    add_header("properties");
    const bool running = state_.run.running();
    for (const property_info& p : info->properties) {
        QWidget* row = add_row();
        auto* label = new QLabel(QString::fromStdString(p.name), row);
        if (!p.persistent) {
            label->setText(label->text() + " (на время сеанса)");
            QFont f = label->font();
            f.setItalic(true);
            label->setFont(f);
        }
        row->layout()->addWidget(label);

        // Текущее значение: на ходу — с живого модуля (find_property из
        // property_override.hpp), иначе из документа; нет в документе —
        // дефолт из описания. Повторный резолв на refresh дешёв: формы малы.
        QString current = QString::fromStdString(p.default_value);
        for (const auto& [pname, pvalue] : m.properties) {
            if (pname == p.name) {
                current = QString::fromStdString(runtime::detail::scalar_to_string(pvalue));
            }
        }
        const std::string module_path = detail::full_path(state_.current_group, m.name);
        if (running) {
            if (atp::group* root = state_.run.live_root()) {
                try {
                    current = QString::fromStdString(
                        atp::runtime::find_property(*root, module_path, p.name).to_string());
                } catch (const std::exception&) {
                    // рассинхрон документа и запуска — показываем документное значение
                }
            }
        }

        auto* edit = make_editor(p, current, row);  // QCheckBox для boolean, QLineEdit иначе
        row->layout()->addWidget(edit->widget);
        auto* apply = new QPushButton("Set", row);
        row->layout()->addWidget(apply);
        auto* clear = new QPushButton("Reset", row);
        row->layout()->addWidget(clear);
        const std::string prop_name = p.name;
        const std::string child_name = m.name;
        const bool persistent = p.persistent;
        QObject::connect(apply, &QPushButton::clicked, this, [this, module_path, child_name, prop_name, persistent, edit] {
            guard("property", [&] {
                const std::string text = edit->text();
                if (state_.run.running()) {
                    state_.run.set_property({module_path, prop_name, text});
                }
                if (persistent) {
                    state_.doc.set_property(state_.current_group, child_name, prop_name,
                                            editor_to_json(*edit));  // скаляр по kind
                }
            });
        });
        const std::string default_value = p.default_value;
        QObject::connect(clear, &QPushButton::clicked, this,
                         [this, module_path, child_name, prop_name, default_value] {
            guard("property", [&] {
                if (state_.run.running()) {
                    // живой модуль узнаёт об откате тем же каналом записи
                    state_.run.set_property({module_path, prop_name, default_value});
                }
                state_.doc.clear_property(state_.current_group, child_name, prop_name);
            });
        });
    }
}
```

Пара «виджет по kind» (приватные хелперы inspector_widget; редактор — struct с указателем на виджет):

```cpp
namespace {

// Редактор значения. Первым делом смотрим на набор: непустой — выпадающий
// список независимо от kind (произвольное значение вводить нельзя, вариант
// выбирается). Прецедент комбобокса по фиксированному списку в файле уже
// есть: режимы потоков, inspector_widget.cpp:199. Без набора решает kind:
// boolean — чекбокс, остальное — строка ввода.
struct property_editor {
    QWidget* widget = nullptr;
    QCheckBox* check = nullptr;  // заполнен для boolean без набора
    QComboBox* combo = nullptr;  // заполнен для любой проперти с набором
    QLineEdit* line = nullptr;   // заполнен для number/text без набора
    atp::io::property_kind kind = atp::io::property_kind::text;

    [[nodiscard]] std::string text() const {
        if (combo != nullptr) {
            return combo->currentText().toStdString();  // текст пункта каноничен: его дал to_string
        }
        if (check != nullptr) {
            return check->isChecked() ? "true" : "false";
        }
        return line->text().toStdString();
    }
};

property_editor* make_editor(const atp::studio::property_info& p, const QString& current, QWidget* parent) {
    auto* editor = new property_editor;  // время жизни — связки connect; хранить в векторе строк
    editor->kind = p.kind;
    if (!p.options.empty()) {
        editor->combo = new QComboBox(parent);
        for (const std::string& option : p.options) {
            editor->combo->addItem(QString::fromStdString(option));
        }
        // setCurrentText на редактируемом комбобоксе завёл бы левый пункт;
        // findText отдаёт -1 на рассинхроне документа с описанием модуля —
        // тогда остаётся первый вариант, а Set перезапишет значение явно.
        const int index = editor->combo->findText(current);
        editor->combo->setCurrentIndex(index < 0 ? 0 : index);
        editor->widget = editor->combo;
    } else if (p.kind == atp::io::property_kind::boolean) {
        editor->check = new QCheckBox(parent);
        editor->check->setChecked(current == "true");
        editor->widget = editor->check;
    } else {
        editor->line = new QLineEdit(current, parent);
        editor->widget = editor->line;
    }
    return editor;
}

// Текст редактора → JSON-скаляр для документа (обратная сторона
// scalar_to_string). Мусор в number — config_error через guard.
nlohmann::json editor_to_json(const property_editor& e) {
    const std::string text = e.text();
    switch (e.kind) {
        case atp::io::property_kind::number:
            try {
                return nlohmann::json::parse(text);
            } catch (const nlohmann::json::parse_error&) {
                throw atp::runtime::config_error("'" + text + "' is not a number");
            }
        case atp::io::property_kind::boolean:
            return nlohmann::json(text == "true");
        case atp::io::property_kind::text:
            break;
    }
    return nlohmann::json(text);
}

}  // namespace
```

Владение `property_editor` — по месту: простейший вариант — `std::vector<std::unique_ptr<property_editor>>` членом inspector_widget, очищаемым в начале `refresh()` (лямбды connect живут не дольше своих виджетов — виджеты умирают при перестройке формы вместе с connect'ами).

- [ ] **Step 2: разлочить секцию на ходу** — в `inspector_widget::refresh` сейчас `body_->setEnabled(!locked)` глушит всё. Изменить: структурные секции складывать в отдельный контейнер `structure_`, а property-строки — вне его; `structure_->setEnabled(!locked)`. Минимальный вариант: `build_module_section` строит property-строки последними и после `body_->setEnabled(!locked)` возвращает им `setEnabled(true)` (строки хранят указатели в локальном векторе). Выбрать по месту, зафиксировать комментарием.
- [ ] **Step 3: save на ходу + sync** — `main_window.cpp`: в `refresh_all` строки 64-65 → `save_action_->setEnabled(true); save_as_action_->setEnabled(true);` (сохранение больше не заперто исполнением). В `save(bool)` перед `state_.doc.save(target)`:

```cpp
        // На ходу сначала стянуть persistent-проперти живых модулей:
        // правки на лету и самозапись модулей попадают в файл.
        if (state_.run.running()) {
            if (atp::group* root = state_.run.live_root()) {
                sync_persistent_properties(state_.doc, state_.doc.config(), *root);
            }
        }
```

(+ include `<atp/studio/property_sync.hpp>`.)

- [ ] **Step 4: собрать всё** (цель по умолчанию собирает и studio при включённом `ATP_BUILD_STUDIO`); полный прогон тестов — PASS. GUI-часть — ручная проверка пользователем (студия: правка проперти в остановленном и запущенном состоянии, выпадающий список у обеих проперти-перечислений — enum и набора значений, сохранение на ходу).
- [ ] **Step 5: стоп-точка** — дифф пользователю + список ручных проверок из Step 4.

---

### Task 13: документация

**Files:**
- Modify: `.claude/CLAUDE.md` (раздел «Архитектура»)
- Modify: `docs/architecture.md`

- [ ] **Step 1: CLAUDE.md** — в списке io-заголовков добавить пункты `property_codec.hpp`/`enum_names.hpp`/`property_base.hpp`/`property.hpp`/`properties.hpp` (по образцу соседних: 1-3 предложения о контракте каждого). Отдельно — понятие перечисления: непустой `property_base::options()`, два способа его объявить (таблица имён `enum_names<E>` для enum-типа, `allowed(...)` в объявлении порта для любого другого) и инвариант «значение всегда внутри набора»; подчеркнуть, что `property_kind` — только JSON-тип и виджет по нему выбирается лишь у пропертей без набора. В пункте `ports.hpp` — третья секция `props`; в `module.hpp`/`module_base.hpp` — `properties()`; в `watcher.hpp` — правило пропертей; удалить упоминания `module_config` (пункты `module_factory*`, `plugin.hpp`); в разделе рантайма — `property_override.hpp`, схема конфига 1.1, узел `properties`; у studio — property_info/`property_sync.hpp`.
- [ ] **Step 2: docs/architecture.md** — те же правки: `grep -n "module_config\|params" docs/architecture.md` и переписать найденные абзацы под проперти.
- [ ] **Step 3: стоп-точка** — дифф пользователю; предложить финальный полный прогон и коммит (пользователь делает сам).

---

## Порядок и зависимости

Задачи 1→1b→2→3→4→5 — платформа, строго последовательно (1b нужна Task 3: `property<T>` переносит `options()` enum-кодека в набор экземпляра). 6→7 — рантайм-свап (6 требует 4). 8 — после 7 (create() без параметров). 9, 10 — независимы после 6, можно в любом порядке. 11 — после 8 и 10. 12 — после 9, 10, 11. 13 — последней.
