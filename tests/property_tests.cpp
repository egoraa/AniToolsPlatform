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
    EXPECT_EQ(limit.get(), 42);  // get не гасит флаг
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
    EXPECT_EQ(limit.get(), 0);      // отказ не трогает значение
    EXPECT_FALSE(limit.changed());  // и не взводит флаг
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
