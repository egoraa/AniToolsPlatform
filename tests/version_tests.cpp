#include <gtest/gtest.h>

#include <atp/version.hpp>

// Compile-time доступ: структурный тип обязан сравниваться в constexpr.
static_assert(atp::version{1, 2} == atp::version{1, 2, 0});
static_assert(atp::version{1, 2, 3} < atp::version{1, 10});
static_assert(atp::default_version == atp::version{0, 0, 1});

// Сахар ver<"...">: разбор строки целиком на этапе компиляции.
static_assert(atp::ver<"1.2.3"> == atp::version{1, 2, 3});
static_assert(atp::ver<"1.2"> == atp::version{1, 2});
static_assert(atp::ver<"1.2">.count == 2);
static_assert(atp::ver<"0.0.1"> == atp::default_version);
static_assert(atp::ver<"10.20.30.40"> == atp::version{10, 20, 30, 40});
static_assert(atp::ver<"7">.count == 1);

TEST(Version, SemverOrdering) {
    EXPECT_LT(atp::version(1, 2, 3), atp::version(1, 2, 4));
    EXPECT_LT(atp::version(1, 2, 3), atp::version(1, 10, 0));
    EXPECT_LT(atp::version(1, 2, 3), atp::version(2, 0, 0));
    EXPECT_EQ(atp::version(1, 2, 3), atp::version(1, 2, 3));
}

TEST(Version, MissingPartsAreZeros) {
    EXPECT_EQ(atp::version(1, 2), atp::version(1, 2, 0));
    EXPECT_LT(atp::version(1, 2), atp::version(1, 2, 1));
    EXPECT_GT(atp::version(1, 2), atp::version(1, 1, 9));
}

TEST(Version, CountIsPreservedButIgnoredInComparison) {
    atp::version v{1, 2};
    EXPECT_EQ(v.count, 2u);
    EXPECT_EQ(atp::version(1, 2, 0).count, 3u);
    EXPECT_EQ(v, atp::version(1, 2, 0));  // count различен, версии равны
}

TEST(Version, FourPartsSupported) {
    EXPECT_LT(atp::version(1, 2, 3, 4), atp::version(1, 2, 3, 5));
    EXPECT_EQ(atp::version(1, 2, 3, 4).count, 4u);
}

TEST(Version, DefaultConstructedIsAllZeros) {
    EXPECT_EQ(atp::version{}, atp::version(0, 0, 0, 0));
    EXPECT_EQ(atp::version{}.count, 0u);
}

TEST(Version, ToStringUsesDeclaredPartCount) {
    EXPECT_EQ(atp::version(1, 2, 3).to_string(), "1.2.3");
    EXPECT_EQ(atp::version(1, 2).to_string(), "1.2");
    EXPECT_EQ(atp::version(1, 2, 3, 45).to_string(), "1.2.3.45");
    EXPECT_EQ(atp::version(0, 0, 1).to_string(), "0.0.1");
}

TEST(Version, ToStringOfDefaultConstructedIsZero) {
    EXPECT_EQ(atp::version{}.to_string(), "0");
}

TEST(Version, VerSugarRoundTripsThroughToString) {
    EXPECT_EQ(atp::ver<"1.2.3">.to_string(), "1.2.3");
    EXPECT_EQ(atp::ver<"10.20">.to_string(), "10.20");
}
