# Module Config Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Дать модулю второй канал настройки — структурированное значение, приходящее в конструктор, то есть раньше `connect` и `initialize`.

**Architecture:** Новый тип-значение `atp::config_value` в SDK (без зависимостей, без парсера); `module_factory_base::create()` получает его параметром; конфиг задаётся в узле модуля объектом на месте или строкой-ссылкой на верхнеуровневый блок `configs`; C-путь читает дерево дописанными в `atp_api` pull-аксессорами, мосты один раз материализуют его в родной словарь.

**Tech Stack:** C++23, header-only, CMake ≥ 4.1, googletest, nlohmann/json (только в `atp_runtime`), CPython 3.11+ и PUC-Lua 5.4 (мосты), Qt 6 Widgets (студия).

Спека: `docs/wip/2026-08-09-module-config-design.md`.

**Правки по ревю 2026-08-10** (перечислены, чтобы исполнитель не «починил» их обратно): рабочее дерево сборки и второй тестовый таргет в Global Constraints; отдельный value-возвращающий guard в Task 6; семь прямых `factory.create()` в Task 4 и два места, где номер ABI назван текстом; запись `configs` обязана быть объектом; порядок ключей описан тем, что он есть; текст конфига в C-пути живёт всё время жизни модуля; Task 9 перенесён из виджета в ядро.

## Global Constraints

- **Ветка.** `master` не коммитится напрямую. Ветка `feature/module-config` **уже создана** и стоит на текущем мастере — работать в ней, не заводить вторую.
- **Коммит — одна строка.** Ни тела, ни списка, ни трейлера `Co-Authored-By`. Коммит делается **по прямому указанию владельца репозитория**, не по своей инициативе: строки `git commit` в шагах ниже — это готовая формулировка сообщения, а не разрешение коммитить.
- **Комментарии только в заголовках**, только Doxygen `///` на объявлении. В `.cpp` комментариев нет вообще, включая тесты. Комментарий объясняет «почему», а не «как»; «почему» без объявления идёт в `docs/architecture.md`.
- **Стиль:** clang-format Chromium base, отступ 4, предел 120 колонок, обязательные скобки у `if`/циклов. Прогонять **закреплённый** clang-format — тот же, что в CI: `pip install clang-format==22.1.8`. Отдельного бинаря на машине нет (он живёт внутри CLion и не на `PATH`), поэтому шаги «Формат» без этой установки не выполнимы; альтернатива — таргет `format` или форматирование через IDE.
- **Именование:** snake_case у файлов/типов/функций/переменных, `value_` у членов, `_base` у стираемых баз, `try_` у не бросающих вариантов, `T`-префикс у шаблонных параметров. Имена gtest-сьютов и тестов — PascalCase.
- `atp_platform` **не имеет права** получить зависимость от nlohmann/json. Всё, что знает про JSON, живёт в `atp_runtime`.
- `include/atp/io.hpp` — зонтик **только** io-слоя; `config_value.hpp` в него не входит.
- Заголовки подхватываются `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)` — новый заголовок не требует правки `CMakeLists.txt`.
- Канонический стиль включения — `<atp/...>` отовсюду.
- **Сборка и прогон — владельца репозитория**, он делает это в CLion. Шаги ниже говорят, что собрать и чем отфильтровать прогон; команду предлагать, а не запускать по своей инициативе.
- Живое дерево сборки здесь одно — `cmake-build-debug` (Ninja, **single-config**, `CMAKE_BUILD_TYPE=Debug`). `build/windows-msvc` — мёртвый остаток конфигурации генератором Visual Studio без `CMakeCache.txt`; собирать и реконфигурировать в нём нельзя. Single-config означает, что `--config Debug` / `-C Debug` — no-op, а бинарь лежит **без** подкаталога конфигурации. Кэш указывает прямо на `cl.exe`, поэтому сборке нужна среда MSVC:

```powershell
cmd /c '"C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 && cmake --build cmake-build-debug --target atp_tests'
cmake-build-debug\tests\atp_tests.exe --gtest_brief=1
```

  Подмножество — `--gtest_filter='ConfigValue.*'`. Второй тестовый таргет — `atp_ui_tests` (`cmake-build-debug\tests\atp_ui_tests.exe`), он нужен только Task 9 и требует живой сессии рабочего стола. Дальше в шагах пишется «собрать `atp_tests`, прогнать `--gtest_filter='X'`» — обёртка и путь берутся отсюда, чтобы не повторять их двадцать раз.

---

### Task 1: `atp::config_value` — тип значения в SDK

**Files:**
- Create: `include/atp/config_value.hpp`
- Test: `tests/platform/config_value_tests.cpp`

**Interfaces:**
- Consumes: ничего.
- Produces: `atp::config_kind` (`null`, `boolean`, `integer`, `real`, `string`, `array`, `object`); `atp::bad_config : std::runtime_error`; класс `atp::config_value`, где `array_type = std::vector<config_value>` и `object_type = std::vector<std::pair<std::string, config_value>>`.
  - Построение: `config_value()` (null); **не** `explicit` конструкторы `config_value(bool)`, `config_value(double)`, `config_value(std::string)`, `config_value(const char*)`, шаблонный `config_value(TInt)` для `std::integral` кроме `bool`, плюс `config_value(array_type)` и `config_value(object_type)`; статические фабрики `config_value::array(std::initializer_list<config_value>)` и `config_value::object(std::initializer_list<std::pair<std::string, config_value>>)`.
  - Чтение: `kind()`, `is_null/is_bool/is_int/is_double/is_string/is_array/is_object`, `size()`, `operator[](std::size_t)`, `key_at(std::size_t)`, `find(std::string_view)`, `at(std::string_view)`, `try_as_bool/try_as_int/try_as_double/try_as_string`, `as_bool/as_int/as_double/as_string`, `bool_at/int_at/double_at/string_at(std::string_view)` в двух перегрузках (бросающая и с `fallback`), `value<T>(std::string_view, T)`.

- [x] **Step 1: Завести ветку** — сделано: `feature/module-config` существует и стоит на текущем мастере.

- [x] **Step 2: Написать падающий тест**

Создать `tests/platform/config_value_tests.cpp`:

```cpp
// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include <atp/config_value.hpp>

namespace {

using cv = atp::config_value;

cv sample() {
    return cv::object({
        {"channels", cv::array({1, 2})},
        {"rate", 48000.0},
        {"name", "rig"},
        {"muted", true},
    });
}

}  // namespace

TEST(ConfigValue, DefaultIsNull) {
    const cv value;
    EXPECT_TRUE(value.is_null());
    EXPECT_EQ(value.kind(), atp::config_kind::null);
    EXPECT_EQ(value.size(), 0U);
}

TEST(ConfigValue, IntegerAndRealAreDistinct) {
    const cv whole(3);
    const cv real(3.0);
    EXPECT_TRUE(whole.is_int());
    EXPECT_FALSE(whole.is_double());
    EXPECT_TRUE(real.is_double());
    EXPECT_FALSE(real.is_int());
}

TEST(ConfigValue, StringLiteralDoesNotBecomeBoolean) {
    const cv value("rig");
    EXPECT_TRUE(value.is_string());
    EXPECT_FALSE(value.is_bool());
    EXPECT_EQ(value.as_string(), "rig");
    EXPECT_TRUE(sample().at("name").is_string());
}

TEST(ConfigValue, ObjectKeepsDeclarationOrder) {
    const atp::config_value root = sample();
    ASSERT_EQ(root.size(), 4U);
    EXPECT_EQ(root.key_at(0), "channels");
    EXPECT_EQ(root.key_at(1), "rate");
    EXPECT_EQ(root.key_at(2), "name");
    EXPECT_EQ(root.key_at(3), "muted");
}

TEST(ConfigValue, FindAnswersNullptrForMissingKey) {
    const atp::config_value root = sample();
    EXPECT_NE(root.find("rate"), nullptr);
    EXPECT_EQ(root.find("absent"), nullptr);
}

TEST(ConfigValue, AtNamesTheMissingKey) {
    const atp::config_value root = sample();
    EXPECT_THROW((void)root.at("absent"), atp::bad_config);
    try {
        (void)root.at("absent");
        FAIL();
    } catch (const atp::bad_config& e) {
        EXPECT_NE(std::string(e.what()).find("absent"), std::string::npos);
    }
}

TEST(ConfigValue, TypedReadNamesTheKeyAndTheFoundKind) {
    const atp::config_value root = sample();
    EXPECT_EQ(root.int_at("channels", 0), 0);
    try {
        (void)root.int_at("name");
        FAIL();
    } catch (const atp::bad_config& e) {
        const std::string text = e.what();
        EXPECT_NE(text.find("name"), std::string::npos);
        EXPECT_NE(text.find("string"), std::string::npos);
    }
}

TEST(ConfigValue, TryReadsDoNotThrow) {
    const atp::config_value root = sample();
    EXPECT_EQ(root.at("rate").try_as_double(), 48000.0);
    EXPECT_FALSE(root.at("name").try_as_int().has_value());
    ASSERT_TRUE(root.at("name").try_as_string().has_value());
    EXPECT_EQ(*root.at("name").try_as_string(), "rig");
}

TEST(ConfigValue, ArrayIndexes) {
    const atp::config_value root = sample();
    const atp::config_value& channels = root.at("channels");
    ASSERT_TRUE(channels.is_array());
    ASSERT_EQ(channels.size(), 2U);
    EXPECT_EQ(channels[0].as_int(), 1);
    EXPECT_EQ(channels[1].as_int(), 2);
}

TEST(ConfigValue, ValueFallsBackWhenKeyIsAbsentOrWrongType) {
    const atp::config_value root = sample();
    EXPECT_EQ(root.value<std::int64_t>("absent", 7), 7);
    EXPECT_EQ(root.value<std::int64_t>("name", 7), 7);
    EXPECT_EQ(root.value<bool>("muted", false), true);
}
```

Здесь `int_at("channels", 0)` — вторая перегрузка «прочитать по ключу со значением по умолчанию»; она нужна тесту, значит объявляется в шаге 4.

- [x] **Step 3: Прогнать и убедиться, что не собирается**

Собрать `atp_tests`. Ожидание: провал компиляции, `atp/config_value.hpp` не найден.

- [x] **Step 4: Написать `include/atp/config_value.hpp`**

Требования к содержимому:

- `enum class config_kind { null, boolean, integer, real, string, array, object };`
- `class bad_config : public std::runtime_error { public: using std::runtime_error::runtime_error; };`
- Хранилище — `std::variant<std::monostate, bool, std::int64_t, double, std::string, array_type, object_type>`; `object_type` именно `std::vector<std::pair<std::string, config_value>>`, **не** `map`.
- Конструкторы скаляров **не** `explicit`: иначе каждый элемент в `object({...})` пришлось бы называть по имени типа и запись теряет весь смысл. Целочисленный — шаблон по `std::integral` с исключением `bool`, потому что литерал `1` имеет тип `int` и до `std::int64_t` сам не доедет. Символьные типы шаблон захватывает вместе с остальными, так что `config_value('x')` — целое `120`; формы «символ» у конфига нет, и доксблок это говорит, чтобы никто не искал её потом.
- **Перегрузка `config_value(const char*)` обязательна.** Без неё строковый литерал уходит в `config_value(bool)`: указатель конвертируется в `bool` лучше, чем в `std::string`, и `{"name", "rig"}` молча становится `true`. Doxygen-блок этой перегрузки обязан сказать, что она существует именно ради подавления той конверсии, иначе её удалят как избыточную.
- Фабрики `array` и `object` принимают `std::initializer_list` и копируют из него — `initializer_list` отдаёт только `const`-ссылки, перемещение оттуда невозможно; для больших деревьев остаются конструкторы от `array_type`/`object_type`, забирающие владение.
- `size()` отвечает длиной массива или объекта, `0` для всего прочего; `operator[](i)` для массива возвращает элемент, для объекта — значение i-й пары, вне диапазона бросает `bad_config`.
- `key_at(i)` — ключ i-й пары объекта, пустой `string_view` для не-объекта или выхода за диапазон.
- `find` не бросает и отвечает `nullptr`; `at` бросает `bad_config` с текстом `"config: no key '<key>'"`.
- `as_*` бросают `bad_config` с текстом `"config: not a <ожидаемое> (found <найденное>)"`; имена форм — `null`, `boolean`, `integer`, `real`, `string`, `array`, `object`.
- `*_at(key)` = `at(key)` + приведение, но текст ошибки — `"config: '<key>' is not a <ожидаемое> (found <найденное>)"`. Перегрузка `*_at(key, fallback)` не бросает и возвращает `fallback`, если ключа нет **или** форма другая.
- `value<T>(key, fallback)` — обобщение той же перегрузки для `bool`, `std::int64_t`, `double`, `std::string`.
- Doxygen-блок на классе объясняет **почему** объект упорядочен и **почему** целое отделено от вещественного (`3` — счётчик, а не `3.0`; набор типов портов уже различает `i64` и `f64`). Про порядок написать ровно то, что верно: он даёт воспроизводимость обхода — одни и те же хэндлы плоского индекса C-пути и один и тот же порядок вставки в словарь моста, тот же довод, что у `__newindex`-прокси Lua-пакета. **Не** писать, что сохраняется порядок из файла: документ читается как `nlohmann::json`, то есть на `std::map`, и к моменту преобразования ключи уже отсортированы — доксблок `encode` в `config_model.hpp` сам это признаёт про алиасы `expose`. Обещание, которого тип не выполняет, хуже отсутствующего.
- Doxygen-блок на `bad_config` объясняет, почему полного пути в тексте нет (потребовал бы родительских ссылок внутри типа-значения) и что ключ называет тот вызов, который его знает.

- [x] **Step 5: Прогнать тесты**

Собрать `atp_tests`, прогнать `--gtest_filter='ConfigValue.*'`. Ожидание: 10 тестов, все PASS.

- [x] **Step 6: Формат и коммит**

```bash
clang-format -i include/atp/config_value.hpp tests/platform/config_value_tests.cpp
git add include/atp/config_value.hpp tests/platform/config_value_tests.cpp
git commit -m "Add config_value, the structured setting a module reads in its constructor"
```

---

### Task 2: Преобразование `nlohmann::json` → `config_value`

**Files:**
- Create: `src/runtime/include/atp/runtime/config_value_json.hpp`
- Test: `tests/runtime/config_value_json_tests.cpp`

**Interfaces:**
- Consumes: `atp::config_value`, `atp::config_kind` из Task 1.
- Produces: `atp::runtime::to_config_value(const nlohmann::json&) -> atp::config_value`.

- [x] **Step 1: Написать падающий тест**

Создать `tests/runtime/config_value_json_tests.cpp`:

```cpp
// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <atp/runtime/config_value_json.hpp>

TEST(ConfigValueJson, NullBecomesNull) {
    EXPECT_TRUE(atp::runtime::to_config_value(nlohmann::json()).is_null());
}

TEST(ConfigValueJson, WholeNumberBecomesIntegerAndFractionBecomesReal) {
    EXPECT_TRUE(atp::runtime::to_config_value(nlohmann::json::parse("3")).is_int());
    EXPECT_TRUE(atp::runtime::to_config_value(nlohmann::json::parse("3.0")).is_double());
    EXPECT_TRUE(atp::runtime::to_config_value(nlohmann::json::parse("3.5")).is_double());
}

TEST(ConfigValueJson, ObjectKeepsDocumentOrder) {
    const nlohmann::json doc = nlohmann::json::parse(R"({"zebra": 1, "alpha": 2})");
    const atp::config_value value = atp::runtime::to_config_value(doc);
    ASSERT_EQ(value.size(), 2U);
    EXPECT_EQ(value.key_at(0), "alpha");
    EXPECT_EQ(value.key_at(1), "zebra");
}

TEST(ConfigValueJson, NestedArraysAndObjects) {
    const nlohmann::json doc = nlohmann::json::parse(R"({"rig": {"channels": [1, 2, 6]}})");
    const atp::config_value value = atp::runtime::to_config_value(doc);
    const atp::config_value& channels = value.at("rig").at("channels");
    ASSERT_TRUE(channels.is_array());
    ASSERT_EQ(channels.size(), 3U);
    EXPECT_EQ(channels[2].as_int(), 6);
}

TEST(ConfigValueJson, BooleanAndStringSurvive) {
    const nlohmann::json doc = nlohmann::json::parse(R"({"muted": true, "name": "rig"})");
    const atp::config_value value = atp::runtime::to_config_value(doc);
    EXPECT_TRUE(value.bool_at("muted"));
    EXPECT_EQ(value.string_at("name"), "rig");
}

TEST(ConfigValueJson, UnsignedBeyondSignedRangeIsRefused) {
    const nlohmann::json doc = nlohmann::json::parse(R"({"huge": 18446744073709551615})");
    EXPECT_THROW((void)atp::runtime::to_config_value(doc), atp::bad_config);
}
```

`ObjectKeepsDocumentOrder` ожидает алфавитный порядок намеренно, и это **тест-документация об ограничении**: `nlohmann::json` по умолчанию — `std::map`, то есть порядок ключей отсортирован уже к моменту преобразования, и порядок из файла до модуля не доезжает в принципе. Тест закрепляет только то, что преобразование ничего не переставляет **само** — воспроизводимость обхода, ради которой упорядоченный объект и заведён. Если однажды понадобится порядок автора, чинить придётся не здесь, а типом, которым читается весь документ (см. «Вне объёма» в спеке).

`UnsignedBeyondSignedRangeIsRefused` закрывает единственную форму, которую закрытый набор `config_value` выразить не может: `uint64_t` выше `int64_t` max. Молчаливое переполнение здесь было бы худшим из возможных ответов — конфиг, где написано число, дал бы модулю другое число.

- [x] **Step 2: Прогнать и убедиться, что не собирается**

Собрать `atp_tests`. Ожидание: провал компиляции, `atp/runtime/config_value_json.hpp` не найден.

- [x] **Step 3: Написать заголовок**

`to_config_value` рекурсивно разбирает узел: `is_null` → `config_value{}`, `is_boolean` → `bool`, `is_number_integer`/`is_number_unsigned` → `std::int64_t`, `is_number_float` → `double`, `is_string` → `std::string`, `is_array` → `array_type` по элементам, `is_object` → `object_type` обходом `items()` с сохранением порядка, который даёт итератор.

Единственная ветка отказа: `is_number_unsigned` со значением выше `std::numeric_limits<std::int64_t>::max()` — `bad_config`. Проверять её **до** общей целочисленной, иначе `get<std::int64_t>()` сработает с переполнением.

Doxygen-блок объясняет, **почему** функция живёт здесь, а не рядом с `config_value`: подпись `create()` не может назвать `nlohmann::json`, иначе `atp_platform` утянет его в каждый плагин — то самое, ради чего `atp_runtime` не экспортируется.

- [x] **Step 4: Прогнать тесты**

Собрать `atp_tests`, прогнать `--gtest_filter='ConfigValueJson.*'`. Ожидание: 6 тестов PASS.

- [x] **Step 5: Формат и коммит**

```bash
clang-format -i src/runtime/include/atp/runtime/config_value_json.hpp tests/runtime/config_value_json_tests.cpp
git add src/runtime/include/atp/runtime/config_value_json.hpp tests/runtime/config_value_json_tests.cpp
git commit -m "Convert a JSON node into a config_value, keeping integers apart from reals"
```

---

### Task 3: Формат конфига — модель, грамматика строки, валидатор, схема 3.2

**Files:**
- Modify: `src/runtime/include/atp/runtime/config_model.hpp:23` (версия схемы), `:32-37` (`module_node`), `:73-79` (`config`), `:83-101` (`decode_module`), `:140-161` (`encode_child`), `:205-233` (`decode`), `:240-275` (`encode`)
- Modify: `src/runtime/include/atp/runtime/config_validator.hpp:21-44` (член валидатора и разбор ссылки), `:160` (разрешённые ключи узла модуля), `:240` (разрешённые ключи документа)
- Test: `tests/runtime/config_model_tests.cpp`, `tests/runtime/config_validator_tests.cpp`

**Interfaces:**
- Consumes: ничего из предыдущих задач (модель хранит JSON дословно).
- Produces: `atp::runtime::module_node::config` типа `std::optional<nlohmann::json>`; `atp::runtime::config::configs` типа `std::vector<std::pair<std::string, nlohmann::json>>`; `atp::runtime::parse_config_ref(std::string_view) -> std::optional<std::string>`, отвечающая именем записи для строки без префикса и `std::nullopt` для строки с любым префиксом; `atp::runtime::config_schema_version` = `{3, 2}`.

- [x] **Step 1: Написать падающие тесты модели**

Дописать в `tests/runtime/config_model_tests.cpp`:

```cpp
TEST(ConfigModel, InlineConfigSurvivesRoundTrip) {
    const nlohmann::json doc = nlohmann::json::parse(R"({
        "version": "3.2",
        "pipeline": {"modules": [{"module": "resampler", "config": {"channels": [1, 2]}}]}
    })");
    EXPECT_EQ(atp::runtime::encode(atp::runtime::decode(doc)), doc);
}

TEST(ConfigModel, ConfigReferenceSurvivesRoundTripUnexpanded) {
    const nlohmann::json doc = nlohmann::json::parse(R"({
        "version": "3.2",
        "configs": {"rig": {"channels": [1, 2]}},
        "pipeline": {"modules": [{"module": "resampler", "config": "rig"}]}
    })");
    const nlohmann::json back = atp::runtime::encode(atp::runtime::decode(doc));
    EXPECT_EQ(back, doc);
    EXPECT_TRUE(back["pipeline"]["modules"][0]["config"].is_string());
}

TEST(ConfigModel, AbsentConfigIsNotWrittenBack) {
    const nlohmann::json doc = nlohmann::json::parse(R"({
        "version": "3.2",
        "pipeline": {"modules": [{"module": "resampler"}]}
    })");
    const nlohmann::json back = atp::runtime::encode(atp::runtime::decode(doc));
    EXPECT_FALSE(back["pipeline"]["modules"][0].contains("config"));
}
```

- [x] **Step 2: Написать падающие тесты валидатора**

Дописать в `tests/runtime/config_validator_tests.cpp`:

```cpp
TEST(ConfigValidator, ConfigMustBeObjectOrString) {
    const nlohmann::json doc = nlohmann::json::parse(R"({
        "version": "3.2",
        "pipeline": {"modules": [{"module": "m", "config": [1, 2]}]}
    })");
    const std::vector<std::string> errors = atp::runtime::validate(doc);
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_NE(errors[0].find("must be an object or a string"), std::string::npos);
}

TEST(ConfigValidator, ConfigReferenceMustExist) {
    const nlohmann::json doc = nlohmann::json::parse(R"({
        "version": "3.2",
        "configs": {"rig": {}},
        "pipeline": {"modules": [{"module": "m", "config": "absent"}]}
    })");
    const std::vector<std::string> errors = atp::runtime::validate(doc);
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_NE(errors[0].find("absent"), std::string::npos);
}

TEST(ConfigValidator, UnknownConfigPrefixIsNamed) {
    const nlohmann::json doc = nlohmann::json::parse(R"({
        "version": "3.2",
        "pipeline": {"modules": [{"module": "m", "config": "file:rig.json"}]}
    })");
    const std::vector<std::string> errors = atp::runtime::validate(doc);
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_NE(errors[0].find("file"), std::string::npos);
}

TEST(ConfigValidator, ConfigsKeyMayNotContainAColon) {
    const nlohmann::json doc = nlohmann::json::parse(R"({
        "version": "3.2",
        "configs": {"a:b": {}},
        "pipeline": {"modules": []}
    })");
    const std::vector<std::string> errors = atp::runtime::validate(doc);
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_NE(errors[0].find("a:b"), std::string::npos);
}

TEST(ConfigValidator, ConfigsContentIsNotSchemaChecked) {
    const nlohmann::json doc = nlohmann::json::parse(R"({
        "version": "3.2",
        "configs": {"rig": {"anything": [1, "two", {"three": null}]}},
        "pipeline": {"modules": [{"module": "m", "config": "rig"}]}
    })");
    EXPECT_TRUE(atp::runtime::validate(doc).empty());
}

TEST(ConfigValidator, ConfigsEntryMustBeAnObject) {
    const nlohmann::json doc = nlohmann::json::parse(R"({
        "version": "3.2",
        "configs": {"rig": [1, 2]},
        "pipeline": {"modules": []}
    })");
    const std::vector<std::string> errors = atp::runtime::validate(doc);
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_NE(errors[0].find("rig"), std::string::npos);
}

TEST(ConfigValidator, BrokenConfigsBlockDoesNotAlsoBlameEveryReference) {
    const nlohmann::json doc = nlohmann::json::parse(R"({
        "version": "3.2",
        "configs": [],
        "pipeline": {"modules": [{"module": "m", "config": "rig"}]}
    })");
    const std::vector<std::string> errors = atp::runtime::validate(doc);
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_NE(errors[0].find("configs"), std::string::npos);
}

TEST(ConfigValidator, SchemaThreeOneStillLoads) {
    const nlohmann::json doc = nlohmann::json::parse(R"({
        "version": "3.1",
        "pipeline": {"modules": [{"module": "m"}]}
    })");
    EXPECT_TRUE(atp::runtime::validate(doc).empty());
}
```

- [x] **Step 3: Прогнать и убедиться, что падает**

Собрать `atp_tests`, прогнать `--gtest_filter='ConfigModel.*:ConfigValidator.*'`. Ожидание: новые тесты FAIL — `"config"` пока отвергается как незнакомый ключ, `"configs"` тоже.

- [x] **Step 4: Поднять версию схемы**

`config_model.hpp:23`: `inline constexpr version config_schema_version{3, 2};`. Doxygen-блок дополнить одной фразой: минорный шаг, потому что ключ добавляется, а форма существующих не меняется.

- [x] **Step 5: Расширить модель**

- `module_node` (`:32`) получает `std::optional<nlohmann::json> config;` с Doxygen-блоком: хранится **дословно как записано**, потому что разворачивать ссылку в объект при сохранении нельзя, а хранение исходного написания — самый дешёвый способ этого не сделать.
- `config` (`:73`) получает `std::vector<std::pair<std::string, nlohmann::json>> configs;`.
- `decode_module`: `if (j.contains("config")) { m.config = j.at("config"); }`.
- `encode_child`: `if (c.module->config) { j["config"] = *c.module->config; }`.
- `decode`: обойти `doc.value("configs", nlohmann::json::object()).items()` в `cfg.configs`.
- `encode`: если `cfg.configs` не пуст, собрать объект и положить в `doc["configs"]`.

- [x] **Step 6: Написать грамматику ссылки**

В `config_model.hpp`, рядом с моделью:

```cpp
[[nodiscard]] inline std::optional<std::string> parse_config_ref(std::string_view text) {
    const std::size_t colon = text.find(':');
    if (colon == std::string_view::npos) {
        return std::string(text);
    }
    return std::nullopt;
}
```

Doxygen-блок обязан сказать: разбор по **первому** двоеточию, тот же приём, что у `parse_property_override`; отсутствие префикса — умолчание, то есть запись верхнеуровневого блока; любой префикс сейчас не поддержан, и новый источник впоследствии стоит один `case` и ноль правок схемы.

- [x] **Step 7: Расширить валидатор**

Класс называется `detail::validator` (`:21`), функция `validate` — свободная (`:233`); `parse_config_ref` виден, потому что `config_validator.hpp` уже включает `config_model.hpp`.

- Члены класса `validator`: `std::unordered_set<std::string> config_names;` и `bool configs_usable = true;` — заполняются в `validate()` до обхода пайплайна, потому что `check_child` иначе не видит корня документа.
- `validate()` (`:240`): в список ключей документа добавить `"configs"`; после `check_version` разобрать `"configs"`: не объект — ошибка **и `configs_usable = false`**; иначе каждый ключ на отсутствие `':'` с текстом, называющим ключ, каждое значение — на `is_object()` с текстом, называющим запись; имена сложить в `config_names`.
- Проверка ссылки ниже выполняется **только при `configs_usable`**. Иначе сломанный блок даёт вторую ошибку на каждую ссылку в документе — про висячее имя, которого нигде не написано; причину надо назвать один раз. Именно это закрепляет `BrokenConfigsBlockDoesNotAlsoBlameEveryReference`, и именно поэтому тесты валидатора здесь сверяют точное число ошибок, а не «непусто».
- `check_child` (`:160`): в `check_keys` добавить `"config"`; затем

```cpp
if (node.contains("config")) {
    const nlohmann::json& cfg = node.at("config");
    const std::string cpath = path + ".config";
    if (cfg.is_object()) {
    } else if (cfg.is_string()) {
        const std::string text = cfg.get<std::string>();
        const std::optional<std::string> ref = parse_config_ref(text);
        if (!ref) {
            error(cpath, "unknown config source '" + text.substr(0, text.find(':')) + "'");
        } else if (configs_usable && !config_names.contains(*ref)) {
            error(cpath, "no entry named '" + *ref + "' in 'configs'");
        }
    } else {
        error(cpath, "must be an object or a string naming an entry of 'configs'");
    }
}
```

- [x] **Step 8: Прогнать тесты**

Собрать `atp_tests`, прогнать `--gtest_filter='ConfigModel.*:ConfigValidator.*:ConfigLoader.*'`. Ожидание: все PASS, включая прежние тесты обоих файлов.

- [x] **Step 9: Формат и коммит**

```bash
clang-format -i src/runtime/include/atp/runtime/config_model.hpp src/runtime/include/atp/runtime/config_validator.hpp tests/runtime/config_model_tests.cpp tests/runtime/config_validator_tests.cpp
git add src/runtime/include/atp/runtime/config_model.hpp src/runtime/include/atp/runtime/config_validator.hpp tests/runtime/config_model_tests.cpp tests/runtime/config_validator_tests.cpp
git commit -m "Carry a module config in the schema, inline or by reference to a shared block"
```

---

### Task 4: `create(const config_value&)` — подпись, ABI и все вызывающие

**Files:**
- Modify: `include/atp/module_factory_base.hpp:29-35`, `include/atp/module_factory.hpp:26-58`, `include/atp/module_registry.hpp:81-91` и `:203-206`, `src/runtime/include/atp/c_module.hpp:357-376` и `:762-764`, `include/atp/plugin.hpp:13-28`, `templates/plugin/CMakeLists.txt:16-18`, `templates/plugin/README.md:17`, `cmake/AniToolsPlatformConfig.cmake.in:21`, `src/studio/include/atp/studio/module_manager.hpp:223`
- Test: `tests/platform/module_factory_tests.cpp` — **не только дописать, но и починить семь существующих прямых вызовов** `factory.create()` на строках 85, 88, 95, 100, 107, 108, 118

**Interfaces:**
- Consumes: `atp::config_value` из Task 1.
- Produces: `atp::module_factory_base::create(const config_value&) const -> module_ptr` (чисто виртуальная); `atp::module_registry::create(const std::string&) const` и `create(const std::string&, const version&) const` — перегрузки с пустым конфигом; `atp::module_registry::create(const std::string&, const config_value&) const` и `create(const std::string&, const version&, const config_value&) const`; `atp::plugin_abi` = 11.

- [x] **Step 1: Написать падающий тест**

Дописать в `tests/platform/module_factory_tests.cpp`:

```cpp
namespace {

class config_reading_module : public atp::module<atp::io::ports<>, "config_reader", atp::version{1, 0}> {
   public:
    explicit config_reading_module(const atp::config_value& cfg) : channels_(cfg.int_at("channels", 0)) {}

    [[nodiscard]] std::int64_t channels() const {
        return channels_;
    }

    atp::work_status iterate(std::stop_token) override {
        return atp::work_status::idle;
    }

   private:
    std::int64_t channels_;
};

}  // namespace

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
```

`configured_module` уже есть в этом файле (используется на `:118`) — новый тест опирается на него, а не заводит второй.

- [x] **Step 2: Прогнать и убедиться, что не собирается**

Собрать `atp_tests`. Ожидание: `create` не принимает аргумента.

Тут же станет видно, что **ломаются семь существующих вызовов в этом же файле** (85, 88, 95, 100, 107, 108, 118): они зовут `factory.create()` напрямую, а не через реестр, поэтому удобная перегрузка без параметра их не спасает. Их надо переписать на `create(atp::config_value{})`. Именно переписать, а не давать `module_factory` перегрузку без аргумента: на реестре она обслуживает вызывающих, которым конфиг взять негде, а на фабрике означала бы, что вызывающий может бесшумно потерять конфиг, который у него был.

- [x] **Step 3: Поменять подпись в базе**

`module_factory_base.hpp:35` → `[[nodiscard]] virtual module_ptr create(const config_value& cfg) const = 0;`. Doxygen-блок переписать: он сейчас прямо обещает «Takes no parameters», и это перестало быть правдой. Новый текст объясняет, **почему** конфиг приходит сюда, а не в `initialize`: конструктор — единственная точка раньше `connect`, и именно там вызываются `make<>()`, объявляющие порты. Добавить `#include <atp/config_value.hpp>`.

- [x] **Step 4: Пробросить конфиг в типизированной фабрике**

`module_factory.hpp:56`:

```cpp
[[nodiscard]] module_ptr create(const config_value& cfg) const override {
    return std::apply(
        [&cfg](const TArgs&... args) {
            if constexpr (std::constructible_from<TModule, const TArgs&..., const config_value&>) {
                return module_ptr(new TModule(args..., cfg), {});
            } else {
                return module_ptr(new TModule(args...), {});
            }
        },
        args_);
}
```

Doxygen-блок класса дополнить: конфиг встаёт **после** привязанных при регистрации аргументов, и модуль подключается к каналу, только если объявил конструктор, который его принимает — иначе не меняется ни на строку.

- [x] **Step 5: Пробросить у остальных наследников и вызывающих**

- `module_registry.hpp:83-91`: две формы `create` получают `const config_value& cfg` и передают его в `at(...).create(cfg)`; рядом — по перегрузке без параметра, вызывающей ту же с `config_value{}`. Doxygen перегрузок объясняет, **почему** умолчание не поставлено прямо на виртуальной функции: у виртуальных оно берётся от статического типа и это классическая ловушка.
- `module_registry.hpp:203`: `pinned_factory::create(const config_value& cfg)` → `inner_->create(cfg)`.
- `c_module.hpp:762`: `c_module_factory::create(const config_value& cfg)` → `new c_module(*desc_, cfg)`.
- `c_module.hpp:364`: конструктор `c_module` получает второй параметр `const config_value& cfg` и кладёт его в новый член `config_value config_;` **до** `build_inputs()`, то есть задолго до `desc.create(...)` на `:372`. Пока член только хранится — читать его будет Task 6.
- `module_manager.hpp:223`: `factory.create(atp::config_value{})`. Это место, где студия создаёт пробный экземпляр, чтобы прочитать список портов; когда порты научатся зависеть от конфига, править придётся именно здесь — записать это одной фразой в Doxygen-блок функции.
- `tests/platform/module_factory_tests.cpp`: семь вызовов из шага 2.

- [x] **Step 6: Поднять ABI**

- `plugin.hpp:28`: `inline constexpr unsigned plugin_abi = 11;`. Строку менять, не переформатируя: `cmake/Install.cmake` вычитывает значение отсюда и ждёт ровно эту форму.
- `plugin.hpp:13-22`, перечень «что менялось»: дописать `11 — create() takes a config_value`. И **сказать, чем это не пункт 6**, который гласит «create(config) carrying per-instance parameters as a config string»: иначе история читается как круг. Тогда через `create` ехали пер-экземплярные скаляры, и пункт 8 заменил их свойствами по делу — свойства видны в инспекторе, правятся живыми и имеют кодек в строку. Ни одно из этих улучшений не касается значения, которое не скаляр, нужно раньше `initialize` и живым не правится; 11 возвращает не отменённое, а закрывает оставшуюся нишу.
- `templates/plugin/CMakeLists.txt:18`: `atp_require_plugin_abi(11)`.
- `templates/plugin/README.md:17` и `cmake/AniToolsPlatformConfig.cmake.in:21`: тот же номер назван текстом и в примере доккомментария. Первый — пользовательский, второй уезжает в установленный пакет, так что оба врут заметно.

- [x] **Step 7: Собрать всё дерево и прогнать тесты**

Собрать всё дерево (не только `atp_tests`), прогнать `--gtest_brief=1`. Ожидание: собирается целиком (студия, мосты, хосты, примеры), все тесты PASS. Компилятор назовёт любой вызывающий, пропущенный в шаге 5, — но полагаться на это как на единственный способ найти вызывающих не надо, список закрыт и лежит в Files.

- [x] **Step 8: Формат и коммит**

```bash
clang-format -i include/atp/module_factory_base.hpp include/atp/module_factory.hpp include/atp/module_registry.hpp include/atp/plugin.hpp src/runtime/include/atp/c_module.hpp src/studio/include/atp/studio/module_manager.hpp tests/platform/module_factory_tests.cpp
git add -u
git commit -m "Hand a module its config at creation, bumping plugin_abi to 11"
```

---

### Task 5: Разрешение ссылки в билдере

**Files:**
- Modify: `src/runtime/include/atp/runtime/pipeline_builder.hpp:39-81` (`detail`, `build_group`), `:146-154` (`build_pipeline`)
- Test: `tests/runtime/pipeline_builder_tests.cpp`

**Interfaces:**
- Consumes: `atp::runtime::to_config_value` (Task 2), `module_node::config` и `config::configs` и `parse_config_ref` (Task 3), `module_registry::create(name, cfg)` (Task 4).
- Produces: `atp::runtime::detail::resolve_config(const module_node&, const std::vector<std::pair<std::string, nlohmann::json>>&) -> atp::config_value`; `build_pipeline` и `build` начинают учитывать `cfg.configs`.

- [x] **Step 1: Написать падающий тест**

Дописать в `tests/runtime/pipeline_builder_tests.cpp` (использовать уже заведённый в файле реестр и фабрики; модуль, читающий конфиг, объявить рядом с остальными тестовыми модулями файла):

```cpp
TEST(PipelineBuilder, InlineConfigReachesTheModule) {
    const nlohmann::json doc = nlohmann::json::parse(R"({
        "version": "3.2",
        "pipeline": {"modules": [{"module": "config_reader", "config": {"channels": 6}}]}
    })");
    atp::module_registry registry;
    registry.add(std::make_unique<atp::module_factory<config_reading_module>>("config_reader"));
    atp::pipeline pipe;
    atp::pipeline_runner runner(pipe);
    atp::runtime::build_pipeline(pipe, runner, atp::runtime::decode(doc), registry);
    EXPECT_EQ(dynamic_cast<config_reading_module*>(pipe.root().find_module("config_reader"))->channels(), 6);
}

TEST(PipelineBuilder, ReferencedConfigReachesTheModule) {
    const nlohmann::json doc = nlohmann::json::parse(R"({
        "version": "3.2",
        "configs": {"rig": {"channels": 6}},
        "pipeline": {"modules": [{"module": "config_reader", "config": "rig"}]}
    })");
    atp::module_registry registry;
    registry.add(std::make_unique<atp::module_factory<config_reading_module>>("config_reader"));
    atp::pipeline pipe;
    atp::pipeline_runner runner(pipe);
    atp::runtime::build_pipeline(pipe, runner, atp::runtime::decode(doc), registry);
    EXPECT_EQ(dynamic_cast<config_reading_module*>(pipe.root().find_module("config_reader"))->channels(), 6);
}

TEST(PipelineBuilder, AbsentConfigIsNullNotAnEmptyObject) {
    const nlohmann::json doc = nlohmann::json::parse(R"({
        "version": "3.2",
        "pipeline": {"modules": [{"module": "config_probe"}]}
    })");
    atp::module_registry registry;
    registry.add(std::make_unique<atp::module_factory<config_probing_module>>("config_probe"));
    atp::pipeline pipe;
    atp::pipeline_runner runner(pipe);
    atp::runtime::build_pipeline(pipe, runner, atp::runtime::decode(doc), registry);
    EXPECT_TRUE(dynamic_cast<config_probing_module*>(pipe.root().find_module("config_probe"))->saw_null());
}
```

`config_probing_module` — модуль, конструктор которого принимает `const atp::config_value&` и запоминает `cfg.is_null()`; объявить его рядом с `config_reading_module` в анонимном пространстве имён файла. Метод поиска ребёнка в группе — `find_module` (`group.hpp:176`), а не `find`.

- [x] **Step 2: Прогнать и убедиться, что падает**

Собрать `atp_tests`, прогнать `--gtest_filter='PipelineBuilder.*'`. Ожидание: новые тесты FAIL — конфиг до модуля не доезжает.

- [x] **Step 3: Написать разрешение**

В `detail` рядом с `apply_properties`:

```cpp
[[nodiscard]] inline config_value resolve_config(
    const module_node& node,
    const std::vector<std::pair<std::string, nlohmann::json>>& shared) {
    if (!node.config) {
        return config_value{};
    }
    if (!node.config->is_string()) {
        return to_config_value(*node.config);
    }
    const std::string text = node.config->get<std::string>();
    const std::optional<std::string> ref = parse_config_ref(text);
    if (!ref) {
        throw config_error("unknown config source in '" + text + "'");
    }
    for (const auto& [name, value] : shared) {
        if (name == *ref) {
            return to_config_value(value);
        }
    }
    throw config_error("no entry named '" + *ref + "' in 'configs'");
}
```

Doxygen-блок объясняет, **почему** разрешение живёт в билдере, а не в модели: модель хранит написанное дословно, чтобы круговой обход был бесплатным, и разворачивать ссылку она не вправе.

Валидатор ловит оба броска раньше, но билдер вызывается и из студии, минуя валидацию документа, — поэтому проверки здесь не лишние, а единственные на этом пути.

- [x] **Step 4: Пробросить через `build_group`**

`build_group` сейчас принимает `(group&, const group_node&, module_registry&)`, то есть `shared` — это **четвёртый** параметр: `const std::vector<std::pair<std::string, nlohmann::json>>& shared`, пробрасываемый в рекурсивный вызов для подгрупп. Строка создания становится

```cpp
const config_value cfg = detail::resolve_config(*c.module, shared);
module_ptr m = c.module->factory_version ? registry.create(c.module->factory, *c.module->factory_version, cfg)
                                         : registry.create(c.module->factory, cfg);
```

`build_pipeline` передаёт `cfg.configs`. Обе строки ставятся **внутрь существующего `try`**, который уже оборачивает создание модуля контекстом «module 'x' in group 'y'», — тогда `config_error` из `resolve_config` приезжает с тем же контекстом и нового обращения с ошибками не нужно.

- [x] **Step 5: Прогнать тесты**

Собрать `atp_tests`, прогнать `--gtest_filter='PipelineBuilder.*'`. Ожидание: все PASS, прежние тесты файла в том числе.

- [x] **Step 6: Формат и коммит**

```bash
clang-format -i src/runtime/include/atp/runtime/pipeline_builder.hpp tests/runtime/pipeline_builder_tests.cpp
git add -u
git commit -m "Resolve a module's config reference when building the pipeline"
```

---

### Task 6: C-путь — аксессоры за `struct_size`

**Files:**
- Modify: `include/atp/plugin_c.h` (перечисление `atp_config_kind`, макрос `ATP_CONFIG_NONE`, семь полей в конце `atp_api`), `src/runtime/include/atp/c_module.hpp` (плоский индекс узлов, семь колбэков в `make_api`)
- Modify: `templates/plugin_rust/src/abi.rs`, `tests/platform/plugin_c_layout_tests.cpp`
- Modify: `tests/test_plugin/plugin_c.c`
- Test: `tests/runtime/c_module_tests.cpp`

**Interfaces:**
- Consumes: `c_module::config_` из Task 4.
- Produces: в `atp_api` поля `config_root`, `config_kind`, `config_size`, `config_key_at`, `config_child_at`, `config_find`, `config_value_of` с сигнатурами из спеки; `ATP_CONFIG_NONE` = 0; `c_module::guarded_value<TRet>` рядом с существующим булевым `guarded` (см. шаг 4 — без него `config_kind` собирается и врёт).

- [x] **Step 1: Написать падающий тест**

Дописать в `tests/runtime/c_module_tests.cpp` тест, который строит `c_module` из тестового дескриптора с конфигом `{"channels": [1, 2, 6], "name": "rig"}` и проверяет через колбэки: корень — объект размера 2; `config_find(root, "channels")` даёт массив длины 3, третий элемент — `ATP_KIND_I64` со значением 6; `config_key_at(root, 0)` — `"channels"`; `config_find(root, "absent")` — `ATP_CONFIG_NONE`; `config_child_at` вне диапазона — `ATP_CONFIG_NONE`. Форму теста взять у соседних тестов файла, которые уже строят `c_module` из дескриптора.

Три утверждения в этом тесте обязательны и не являются придирками, потому что без них шаг 4 собирается и молча врёт (см. там же):

- `config_kind(root)` **равен** `ATP_CONFIG_OBJECT`, а не «ненулевой»;
- хэндл `config_find(root, "name")` **не равен** хэндлу корня;
- `config_value_of` на корне (объект) и на узле формы `null` **отказывает**, не тронув `*out`.

- [x] **Step 2: Прогнать и убедиться, что не собирается**

Собрать `atp_tests`. Ожидание: у `atp_api` нет таких полей.

- [x] **Step 3: Расширить заголовок C ABI**

В `plugin_c.h`:

```c
#define ATP_CONFIG_NONE 0u

typedef enum atp_config_kind {
    ATP_CONFIG_NULL = 0,
    ATP_CONFIG_BOOL = 1,
    ATP_CONFIG_INT = 2,
    ATP_CONFIG_REAL = 3,
    ATP_CONFIG_TEXT = 4,
    ATP_CONFIG_ARRAY = 5,
    ATP_CONFIG_OBJECT = 6
} atp_config_kind;
```

и семь полей **в конец** `atp_api`, с сигнатурами из спеки. Doxygen обязан сказать четыре вещи:

- рост через `struct_size` означает, что `ATP_C_ABI` **не** двигается (`plugin_c.h:140` объявляет это штатным способом роста);
- ноль зарезервирован под «узла нет», и корень нулём быть не может, поэтому проверка результата одна и та же у `config_find` и `config_child_at`;
- текст, выданный `config_value_of`, живёт **всё время жизни модуля** — это сильнее контракта портов, и намеренно: текст порта транзиентен потому, что он копия, снятая в общий scratch, а строка конфига уже лежит в дереве `config_value`, которое модуль держит от конструктора до деструктора. Указатель отдаётся прямо в дерево; выдача через scratch дала бы гарантию слабее бесплатно и вдобавок асимметрию, при которой `config_key_at` (указатель в `std::string` дерева) вечен, а значение умирает от постороннего `get_input` на том же `ctx`;
- `config_value_of` осмыслен только для четырёх скалярных форм: в `atp_kind` нет ни null, ни контейнера, поэтому на `null`, массиве и объекте он **отказывает и `*out` не трогает**.

- [x] **Step 4: Реализовать колбэки**

В `c_module` — плоский индекс, собираемый один раз при построении, до `desc.create(...)`:

```cpp
void index_config(const config_value& node) {
    nodes_.push_back(&node);
    for (std::size_t i = 0; i < node.size(); ++i) {
        index_config(node[i]);
    }
}

[[nodiscard]] const config_value* config_at(std::uint32_t node) const {
    return node != ATP_CONFIG_NONE && node <= nodes_.size() ? nodes_[node - 1] : nullptr;
}
```

`nodes_` — `std::vector<const config_value*>`; хэндл узла равен индексу плюс единица, поэтому ноль свободен под `ATP_CONFIG_NONE`, а корень получает 1. Индекс строится **после** того, как `config_` уложен в член: он хранит указатели внутрь дерева, и переприсваивание `config_` его инвалидировало бы. `config_` инициализируется в списке инициализации конструктора (то есть до тела), `index_config` зовётся в теле — до `desc.create(...)` на `:372`, потому что C-модуль читает конфиг именно там, это его аналог конструктора.

**Существующий `guarded` для этих колбэков не годится, и это единственная ловушка задачи с бесшумной ценой.** `c_module.hpp:601` объявлен `static int guarded(atp_ctx*, TFn&&) noexcept` и внутри делает `return body(self) ? 1 : 0`, потому что все восемь нынешних колбэков отвечают успехом или отказом. Через него:

- `config_root`/`config_size`/`config_child_at`/`config_find` возвращают `uint32_t`, лямбда выведет `int`, и преобразования в `uint32_t (*)(...)` не будет — ошибка компиляции;
- `config_kind` возвращает `int`, поэтому **соберётся и будет врать**: `ATP_CONFIG_OBJECT` (6) станет `1`, то есть `ATP_CONFIG_BOOL`, и любая непустая форма отчитается как bool.

Поэтому рядом заводится второй guard, возвращающий значение, а булев остаётся нетронутым — на его `? 1 : 0` держится вся существующая таблица:

```cpp
template <typename TRet, typename TFn>
static TRet guarded_value(atp_ctx* ctx, TRet on_failure, TFn&& body) noexcept {
    if (ctx == nullptr || ctx->owner == nullptr) {
        return on_failure;
    }
    c_module& self = *ctx->owner;
    try {
        return body(self);
    } catch (...) {
        if (self.pending_ == nullptr) {
            self.pending_ = std::current_exception();
        }
        return on_failure;
    }
}
```

Значение отказа передаётся, а не выводится: у хэндлов это `ATP_CONFIG_NONE`, у `config_kind` — `ATP_CONFIG_NULL`, у `config_key_at`/`config_value_of` — `0`, и подставлять `TRet{}` за автора было бы верно ровно в двух случаях из четырёх.

Колбэки пишутся в `make_api()` тем же приёмом, что и соседние. Образец, задающий форму остальным шести:

```cpp
table.config_find = [](atp_ctx* ctx, std::uint32_t node, const char* key, std::size_t len) noexcept {
    return guarded_value<std::uint32_t>(ctx, ATP_CONFIG_NONE, [&](c_module& self) -> std::uint32_t {
        const config_value* parent = self.config_at(node);
        if (parent == nullptr || key == nullptr || !parent->is_object()) {
            return ATP_CONFIG_NONE;
        }
        const config_value* found = parent->find(std::string_view(key, len));
        if (found == nullptr) {
            return ATP_CONFIG_NONE;
        }
        const auto at = std::ranges::find(self.nodes_, found);
        return at == self.nodes_.end()
                   ? ATP_CONFIG_NONE
                   : static_cast<std::uint32_t>(std::distance(self.nodes_.begin(), at) + 1);
    });
};
```

Поиск хэндла по указателю — линейный проход по всему плоскому индексу, и это выбрано сознательно: индекс DFS-preorder, так что хэндлы детей вычислимы и `O(1)` достижим, но конфиг — это десятки узлов, читаемые один раз в конструкторе, и лишняя структура здесь дороже прохода. Записать выбор, а не оставить его похожим на недосмотр.

`config_value_of` **не** пользуется scratch-буфером: строка отдаётся указателем прямо в `std::string` дерева, живущую всё время жизни модуля. Из этого же следует, что колбэк ничего не пишет в `self.scratch_` и потому не может испортить только что прочитанное значение порта.

- [x] **Step 5: Прогнать тест**

Собрать `atp_tests`, прогнать `--gtest_filter='CModule.*'`. Ожидание: PASS.

- [x] **Step 6: Обновить зеркала и модуль-образец**

- `templates/plugin_rust/src/abi.rs`: дописать семь полей в `AtpApi` **в том же порядке**, `ATP_CONFIG_NONE` и константы `atp_config_kind`.
- `tests/platform/plugin_c_layout_tests.cpp`: обновить закреплённый `sizeof(atp_api)`; `sizeof(atp_module_desc)` и `ATP_MODULE_DESC_SIZE_V1` **не трогать** — floor приёмки не двигается никогда.
- `tests/test_plugin/plugin_c.c`: дать одному модулю чтение конфига через новые колбэки, чтобы путь был покрыт на настоящем C.

- [x] **Step 7: Прогнать всё**

Собрать всё дерево, прогнать `--gtest_brief=1`. Ожидание: все PASS.

- [x] **Step 8: Формат и коммит**

```bash
clang-format -i include/atp/plugin_c.h src/runtime/include/atp/c_module.hpp tests/platform/plugin_c_layout_tests.cpp
git add -u
git commit -m "Let a C module read its config through pull accessors on atp_api"
```

---

### Task 7: Мост Python — конфиг как словарь

**Files:**
- Modify: `src/bridges/python/values.hpp`, `src/bridges/python/values.cpp` (обход дерева), `src/bridges/python/instance.cpp` (передача при создании), `src/bridges/python/package/atp/_ports.py` или `__init__.py` (доступ у автора)
- Test: `tests/bridges/python_bridge_tests.cpp`

**Interfaces:**
- Consumes: аксессоры `atp_api` из Task 6.
- Produces: атрибут `self.config` у экземпляра модуля — `dict`, `list`, скаляр или `None`.

- [x] **Step 1: Написать падающий тест**

Дописать в `tests/bridges/python_bridge_tests.cpp` тест: скрипт объявляет модуль, который в `initialize` пишет в лог `self.config["channels"][2]`; хост создаёт его с конфигом `{"channels": [1, 2, 6]}` и ждёт `6` в логе. Форму — у соседних тестов файла; путь скрипта строить от `std::filesystem::temp_directory_path()`.

- [x] **Step 2: Прогнать и убедиться, что падает**

Собрать `atp_tests`, прогнать `--gtest_filter='PythonBridge.*'`. Ожидание: FAIL — атрибута нет.

Если падают **все** тесты моста, а не только новый, — это известная особенность машины, не регрессия: нужны DLL рядом с exe и `PYTHONHOME`.

- [x] **Step 3: Реализовать обход**

В `values.cpp` — рекурсивная функция «узел конфига → `PyObject*`»: объект → `PyDict_New` с сохранением порядка вставки (`dict` его хранит сам), массив → `PyList_New`, скаляры → `PyLong`/`PyFloat`/`PyBool`/`PyUnicode` из UTF-8, null → `Py_None`. Вызвать один раз при создании экземпляра и положить результат атрибутом `config`.

- [x] **Step 4: Прогнать тест**

Прогнать `--gtest_filter='PythonBridge.*'`. Ожидание: PASS.

- [x] **Step 5: Формат и коммит**

```bash
clang-format -i src/bridges/python/values.hpp src/bridges/python/values.cpp src/bridges/python/instance.cpp
git add -u
git commit -m "Give a Python module its config as an ordinary dict"
```

---

### Task 8: Мост Lua — конфиг как таблица

**Files:**
- Modify: `src/bridges/lua/values.hpp`, `src/bridges/lua/values.cpp`, `src/bridges/lua/instance.cpp`, `src/bridges/lua/package/atp.lua`
- Test: `tests/bridges/lua_bridge_tests.cpp`

**Interfaces:**
- Consumes: аксессоры `atp_api` из Task 6.
- Produces: поле `config` у экземпляра модуля — таблица, массив-таблица, скаляр или `nil`.

- [x] **Step 1: Написать падающий тест**

Дописать в `tests/bridges/lua_bridge_tests.cpp` тест-двойник питоновского: скрипт пишет в лог `self.config.channels[3]` (индексация с единицы), хост даёт `{"channels": [1, 2, 6]}`, ожидание — `6`. Имена в тесте строить из числовых кодовых точек, если понадобится не-ASCII: сборка не ставит `/utf-8` тестам-фикстурам с литералами.

- [x] **Step 2: Прогнать и убедиться, что падает**

Собрать `atp_tests`, прогнать `--gtest_filter='LuaBridge.*'`. Ожидание: FAIL.

- [x] **Step 3: Реализовать обход**

Рекурсивно: объект → таблица со строковыми ключами, массив → таблица с ключами `1..n`, скаляры → `lua_pushinteger`/`lua_pushnumber`/`lua_pushboolean`/`lua_pushlstring`, null → `nil`. Помнить правило моста: **ни один кадр между `lua_pcall` и `luaL_error` не владеет объектом с нетривиальным деструктором.**

Порядок ключей здесь теряется — таблица его не хранит, и `__newindex`-прокси, которым мост держит порядок объявления портов, сюда не тянется: у конфига порядок ничего не адресует. Это единственная честная разница с Python и она уходит в документацию (Task 10), а не в код.

- [x] **Step 4: Прогнать тест**

Прогнать `--gtest_filter='LuaBridge.*'`. Ожидание: PASS.

- [x] **Step 5: Формат и коммит**

```bash
clang-format -i src/bridges/lua/values.hpp src/bridges/lua/values.cpp src/bridges/lua/instance.cpp
git add -u
git commit -m "Give a Lua module its config as an ordinary table"
```

---

### Task 9: Студия — мутатор конфига в ядре и панель редактирования

**Files:**
- Modify: `src/studio/include/atp/studio/project.hpp` (мутатор и запрет), `src/studio/ui/panels/inspector_widget.hpp`/`.cpp` (панель)
- Test: `tests/studio/project_tests.cpp` (таргет `atp_tests`), `tests/ui/property_grid_tests.cpp` или новый файл рядом (таргет **`atp_ui_tests`**)

**Interfaces:**
- Consumes: `module_node::config` и `config::configs` из Task 3.
- Produces: мутатор конфига модуля на `project`, отказывающий при работающем пайплайне; панель JSON в инспекторе, гаснущая в том же состоянии.

**Что уже работает и делать не надо.** `project` держит `runtime::config cfg_` и гоняет через `runtime::encode`/`decode` всё: `create` (`project.hpp:49`), `open` (`:67-68`), `save` (`:106-108`) и обе стороны undo/redo (`:728-729`, `:744-745`). Значит после Task 3 конфиг переживает сохранение, открытие и undo **без единой правки проекта** — дословное хранение из Task 3 делает это бесплатно. То же с MCP: `document_tools.hpp:94` отдаёт `runtime::encode(ws.project().config())` целиком, так что конфиг появляется в `get_document` сам, и словарь «document» не трогается. Задача — только мутатор и панель.

- [x] **Step 1: Прочитать API проекта, прежде чем писать тест**

Прочесть `src/studio/include/atp/studio/project.hpp` целиком и `tests/studio/project_tests.cpp`. Точные имена методов сохранения, открытия и обхода модулей берутся **оттуда**; ниже описано, что тест должен утверждать, а не как называются его вызовы. Заодно посмотреть, как сделан мутатор свойства — новый пишется ему в пару и по его образцу.

- [x] **Step 2: Написать падающие тесты ядра** (`tests/studio/`, таргет `atp_tests`)

Два теста.

Первый — круговой обход: собрать проект с двумя модулями, у одного конфиг задан на месте, у другого ссылкой; сохранить, открыть, сравнить с исходным JSON, затем прогнать undo/redo и сравнить снова. Утверждение, ради которого он и пишется: ссылка осталась **строкой** и не развернулась в объект.

Второй — правило работающего пайплайна: при запущенном пайплайне мутатор конфига отвергает правку, тогда как `session::set_property` проходит. Это то место, где два вида настройки ведут себя по-разному, и без теста разница держится на честном слове.

Запрет живёт **в ядре, на `project`**, а не в виджете, по двум причинам: иначе он проверяем только через Qt, то есть в `atp_ui_tests` с живой сессией рабочего стола, — и иначе он не действует для MCP-клиента, который до панели не доходит вовсе. Виджет тогда лишь отражает состояние.

- [x] **Step 3: Прогнать и убедиться, что падает**

Собрать `atp_tests`, прогнать `--gtest_filter='Project.*'`. Ожидание: FAIL.

- [x] **Step 4: Написать мутатор**

Метод `project`, ставящий конфиг модуля по пути (в паре с тем, которым правится свойство), с отказом при работающем пайплайне. Doxygen-блок объясняет **почему** правило другое, чем у свойств: конфиг доезжает до конструктора, значит относится к структуре проекта, а структура при работе read-only. Перевозка через save/open/undo не пишется — см. выше, она уже есть.

- [x] **Step 5: Панель редактирования**

Отдельная область JSON в инспекторе, **не** строка в сетке свойств. Введённый текст разбирается, ошибка показывается, мусор в проект не пишется. При работающем пайплайне поле недоступно — виджет спрашивает у ядра, а не решает сам.

- [x] **Step 6: Тест панели** (`tests/ui/`, таргет `atp_ui_tests`)

`tests/ui` — **отдельный исполняемый файл**, глобится своим `file(GLOB_RECURSE)` в `tests/CMakeLists.txt` и существует только при `atp_studio_ui`; соседи по задаче — `property_grid_tests.cpp` и `property_actions_tests.cpp`, форму брать у них (там же `qt_app.hpp`). Тест: панель показывает конфиг модуля и гаснет при работающем пайплайне.

- [x] **Step 7: Прогнать тесты и проверить окно**

Собрать всё дерево, прогнать `atp_tests --gtest_brief=1` и `atp_ui_tests --gtest_brief=1` (второму нужна живая сессия рабочего стола). Ожидание: все PASS. Окно проверять через UI Automation, запуская студию с подменённым `APPDATA` и удерживая её pid — `Stop-Process` по имени убьёт окно владельца репозитория.

- [x] **Step 8: Формат и коммит**

```bash
clang-format -i src/studio/include/atp/studio/project.hpp src/studio/ui/panels/inspector_widget.hpp src/studio/ui/panels/inspector_widget.cpp
git add -u
git commit -m "Carry a module config through the project and edit it outside the property grid"
```

---

### Task 10: Документация

**Files:**
- Modify: `docs/architecture.md` (новый раздел), `.claude/CLAUDE.md` (раздел «Config and hosts, property paths»)

**Interfaces:**
- Consumes: всё предыдущее.
- Produces: ничего исполняемого.

- [x] **Step 1: Раздел в `docs/architecture.md`**

Написать «Конфиг модуля» на русском, как соседние разделы. Обязательно объяснить **почему**, а не что:

- почему конструктор, а не `initialize`;
- почему собственный тип, а не `nlohmann::json` (SDK не имеет права утянуть его в каждый плагин);
- почему объект упорядочен — воспроизводимость обхода, — и почему это **не** порядок из файла: документ читается как `nlohmann::json`, ключи отсортированы к моменту разбора, и сохранить авторский порядок означало бы читать весь документ как `ordered_json`;
- почему строка — всегда ссылка и почему запись `configs` обязана быть объектом (корень конфига один и тот же при любом написании, иначе модуль пишет код про синтаксис файла);
- почему грамматика разбирается по первому двоеточию;
- почему таблица Lua теряет порядок ключей, а `dict` — нет;
- почему текст конфига в C-пути живёт всё время жизни модуля, тогда как текст порта — до следующего чтения;
- почему `ATP_C_ABI` не двинулся, а `plugin_abi` двинулся — **и чем 11 отличается от отменённого пункта 6**, где `create` уже принимал конфиг строкой: тогда это были пер-экземплярные скаляры, которые свойства делают лучше, и именно поэтому ниша осталась ровно та, что не скаляр.

- [x] **Step 2: Обновить `.claude/CLAUDE.md`**

В разделе про конфиг: схема **3.2**, ключ `"config"` узла модуля и верхнеуровневый `"configs"` (запись — объект), `plugin_abi` **11**, `create()` принимает `config_value`. Держать файл переносимым — ни абсолютных путей, ни имени одной ОС.

- [x] **Step 3: Коммит**

```bash
git add docs/architecture.md .claude/CLAUDE.md
git commit -m "Describe the module config channel and its rationale"
```

- [x] **Step 4: Слить ветку**

Слияние — по прямому указанию владельца репозитория, не по своей инициативе.

---

## Порядок и зависимости

```
1 (config_value)
├── 2 (json → config_value) ──┐
└── 4 (create) ───────────────┼── 5 (билдер)
    └── 6 (C-путь) ── 7 (Python), 8 (Lua)
3 (формат) ────────────────────┘
9 (студия) — после 3
10 (документация) — последней
```

Задачи 7 и 8 независимы друг от друга и могут идти параллельно.

---

## Что выяснилось при реализации

Записано, потому что ни спека, ни план этого не предвидели, а следующая правка тех же мест наткнётся на
то же самое.

- **Ограничение `module_factory` на уровне класса пришлось ослабить.** Было
  `requires std::constructible_from<TModule, const TArgs&...>`, и модуль, у которого единственный
  конструктор берёт конфиг, конструируем из **нуля** аргументов — то есть требование отвергало ровно те
  модули, ради которых канал и заводится. Стало дизъюнкцией двух форм; `if constexpr` внутри `create`
  сам по себе этого не решал.
- **Понадобился `config_value::string_ptr()`.** Гарантия «текст конфига живёт всё время жизни модуля»
  невыполнима через `as_string()`, который возвращает копию: она умрёт до возврата из колбэка. Доступ к
  самой хранимой строке — единственный аксессор без копии, и он существует именно для C-пути.
- **Понадобился `project::set_shared_config`.** Без него форма-ссылка недостижима из студии, и тест
  кругового обхода падает не на круговом обходе, а на валидации при открытии: сохранённая ссылка
  указывает на блок, которого никто не мог объявить. Спека этого не заметила, потому что смотрела на
  перевозку, а не на то, кто создаёт цель ссылки.
- **Запрет «конфиг read-only при работе» живёт в UI, а не в ядре** — вопреки тому, что ревю
  рекомендовало. В студии так устроены **все** структурные правки (`state_.view->running()`), а
  `project` — документ и о раннере не знает; делать конфиг единственным исключением дороже, чем
  следовать заведённому. `apply_lock` инспектора и так гасит всё, кроме блока пропертей, поэтому новой
  машинерии не понадобилось вовсе.
- **Два существующих теста сдвинулись, и оба по делу.** `ConfigValidator.AcceptsEveryMinorUpToTheSupportedOne`
  закреплял, что 3.2 — «из будущего»: граница переехала на 3.3. `c_module_test.RegistersEveryDescribedModule`
  перечисляет модули тестового C-плагина, и их стало четыре.
- **`PyList_SET_ITEM` недоступен под `Py_LIMITED_API`** — обход массива пишется через `PyList_SetItem`,
  которая тоже забирает ссылку.
- **Тесту панели нельзя `findChild` по своему типу**: в UI нет `Q_OBJECT`/moc, и `findChild<property_grid*>`
  не компилируется. Виджеты ищутся по типам Qt.
- **Формат не прогнан:** `clang-format` на этой машине отсутствует (см. Global Constraints), строки
  вручную удержаны в 120 колонок, но джоба `clang-format` в CI — единственная настоящая проверка.
