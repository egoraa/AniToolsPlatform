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

// An enum property: the name table sits next to the enum, and everything else works as for scalars.
enum class blend { normal, add, multiply };

// An enum without a zero option, to exercise the default-value trap.
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
    EXPECT_EQ(limit.take(), std::nullopt);  // the default is not an event
}

TEST(Property, DefaultOfDefaultIsValueInitialized) {
    atp::io::property<int> p("p");
    EXPECT_EQ(p.get(), 0);
}

TEST(Property, WriteRaisesChangedAndTakeConsumesIt) {
    atp::io::property<int> limit("limit", 10);
    limit(42);
    EXPECT_TRUE(limit.changed());
    EXPECT_EQ(limit.get(), 42);  // get does not clear the flag
    EXPECT_TRUE(limit.changed());
    EXPECT_EQ(limit.take(), std::optional(42));
    EXPECT_EQ(limit.take(), std::nullopt);  // the event was handled exactly once
    EXPECT_EQ(limit.get(), 42);             // the value is still there
}

TEST(Property, EveryWriteRaisesFlagEvenIfEqual) {
    atp::io::property<int> p("p", 5);
    p(5);  // the same value is still an event — there is deliberately no comparison
    EXPECT_TRUE(p.changed());
}

TEST(Property, ResetRestoresDefaultAndRaisesFlag) {
    atp::io::property<std::string> file("file", "a.txt");
    file(std::string("b.txt"));
    (void)file.take();
    file.reset();
    EXPECT_EQ(file.get(), "a.txt");
    EXPECT_TRUE(file.changed());  // the module has to learn about the rollback
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
    EXPECT_EQ(limit.get(), 0);      // a refusal leaves the value alone
    EXPECT_FALSE(limit.changed());  // and does not raise the flag
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
    p(12345);  // without a value set nothing constrains the write
    EXPECT_EQ(p.get(), 12345);
}

TEST(Property, EnumPropertyWorksLikeAnyOther) {
    atp::io::property<blend> mode("mode", blend::add);
    EXPECT_EQ(mode.get(), blend::add);
    mode(blend::multiply);
    EXPECT_EQ(mode.take(), std::optional(blend::multiply));

    atp::io::property_base& base = mode;
    EXPECT_EQ(base.kind(), atp::io::property_kind::text);  // an option name is a string
    EXPECT_EQ(base.to_string(), "multiply");
    EXPECT_EQ(base.default_string(), "add");
    base.from_string("normal");
    EXPECT_EQ(mode.get(), blend::normal);
}

// The type's name table fills the instance's value set, after which the source is indistinguishable.
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

// The "value is always inside the set" invariant: anything unrepresentable is rejected on the way
// in, otherwise to_string would break in the inspector and when saving the config.
TEST(Property, EnumRejectsValueOutsideTable) {
    atp::io::property<blend> mode("mode");
    EXPECT_THROW(mode(static_cast<blend>(99)), std::invalid_argument);
    EXPECT_EQ(mode.get(), blend::normal);  // a refusal leaves the value alone
    EXPECT_FALSE(mode.changed());
    EXPECT_THROW((atp::io::property<blend>("bad", static_cast<blend>(99))), std::invalid_argument);
}

// The implicit default is T{}, which for an enum is the value 0. With no zero option in the table
// the constructor refuses right away instead of handing out a property in an unrepresentable state;
// the module author has to spell the default out.
TEST(Property, EnumWithoutZeroVariantDemandsExplicitDefault) {
    EXPECT_THROW((atp::io::property<scale>("scale")), std::invalid_argument);
    EXPECT_NO_THROW((atp::io::property<scale>("scale", scale::full)));
}

// An enumeration without an enum type: the set is declared where the port is.
TEST(Property, AllowedRestrictsScalarProperty) {
    atp::io::property<int> channels("channels", 2, atp::io::allowed(1, 2, 6));
    EXPECT_EQ(channels.options(), (std::vector<std::string>{"1", "2", "6"}));
    EXPECT_EQ(channels.kind(), atp::io::property_kind::number);  // it still reaches the config as a number
    channels(6);
    EXPECT_EQ(channels.get(), 6);
    EXPECT_THROW(channels(3), std::invalid_argument);
    EXPECT_EQ(channels.get(), 6);  // a refusal leaves the value alone
}

TEST(Property, AllowedRestrictsTextPropertyAndFromString) {
    atp::io::property<std::string> codec("codec", "h264", atp::io::allowed("h264", "h265", "av1"));
    codec.from_string("av1");
    EXPECT_EQ(codec.get(), "av1");
    try {
        codec.from_string("vp9");  // parses as a string but falls outside the set
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

// An instance's set narrows the type's table: the module supports only some of the options.
TEST(Property, AllowedNarrowsEnumTableToSubset) {
    atp::io::property<blend> mode("mode", blend::add, atp::io::allowed(blend::add, blend::multiply));
    EXPECT_EQ(mode.options(), (std::vector<std::string>{"add", "multiply"}));
    EXPECT_THROW(mode(blend::normal), std::invalid_argument);  // present in the table, absent from the set
}

// Canonical comparison: "007" parses into 7, and it is 7 that is looked up in the set.
TEST(Property, MembershipIsCheckedOnCanonicalText) {
    atp::io::property<int> level("level", 7, atp::io::allowed(7, 8));
    level.from_string("007");
    EXPECT_EQ(level.get(), 7);
}

// Concurrent reads and writes under the default locking: there is no TSan-like check here, so the
// test at least catches torn values and crashes.
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
