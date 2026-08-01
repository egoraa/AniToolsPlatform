#include <optional>
#include <string>

#include <gtest/gtest.h>

#include <atp/io/property_codec.hpp>

namespace {

struct percent {
    int value = 0;
};

}  // namespace

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
    EXPECT_EQ(atp::io::property_codec<int>::from_string("12x"), std::nullopt);
    EXPECT_EQ(atp::io::property_codec<int>::from_string(""), std::nullopt);
}

TEST(PropertyCodec, DoubleRoundTrip) {
    const std::string text = atp::io::property_codec<double>::to_string(0.5);
    EXPECT_EQ(atp::io::property_codec<double>::from_string(text), std::optional(0.5));
}

TEST(PropertyCodec, BoolIsWordsTrueFalse) {
    EXPECT_EQ(atp::io::property_codec<bool>::to_string(true), "true");
    EXPECT_EQ(atp::io::property_codec<bool>::from_string("false"), std::optional(false));
    EXPECT_EQ(atp::io::property_codec<bool>::from_string("TRUE"), std::nullopt);
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
    static_assert(!atp::io::property_value<void*>);
    EXPECT_EQ(atp::io::property_codec<percent>::from_string("15%")->value, 15);
}

}  // namespace
