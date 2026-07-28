#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include <atp/io/enum_names.hpp>

namespace {

// The option order deliberately differs from the alphabetical one: the table defines the order of
// the drop-down, and it has to survive exactly as declared.
enum class blend { normal, add, multiply };

// An enum without a name table, to check that the concept rejects it.
enum class nameless { a, b };

}  // namespace

// Specialised inside atp::io, exactly as property_codec<percent> is.
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
    static_assert(atp::io::property_value<blend>);  // the general codec contract is satisfied
}

TEST(EnumNames, RoundTripsThroughNames) {
    EXPECT_EQ(blend_codec::to_string(blend::multiply), "multiply");
    EXPECT_EQ(blend_codec::from_string("add"), std::optional(blend::add));
    EXPECT_EQ(blend_codec::kind, atp::io::property_kind::text);  // the name reaches the config as a string
}

TEST(EnumNames, UnknownNameIsRejected) {
    EXPECT_EQ(blend_codec::from_string("screen"), std::nullopt);
    EXPECT_EQ(blend_codec::from_string(""), std::nullopt);
    EXPECT_EQ(blend_codec::from_string("Add"), std::nullopt);  // the case matters, as it does for bool
    EXPECT_EQ(blend_codec::from_string("1"), std::nullopt);    // the numeric form is not accepted
}

TEST(EnumNames, OptionsKeepDeclarationOrder) {
    const std::span<const std::string_view> options = blend_codec::options();
    ASSERT_EQ(options.size(), 3u);
    EXPECT_EQ(options[0], "normal");
    EXPECT_EQ(options[1], "add");
    EXPECT_EQ(options[2], "multiply");
    static_assert(blend_codec::options().size() == 3);  // the list is available at compile time too
}

// A value outside the table is a module bug. The codec does not diagnose it, having no property
// name for a message, but neither does it invent text: an empty string matches no allowed name, so
// property<E> rejects the write.
TEST(EnumNames, ValueOutsideTableHasNoText) {
    EXPECT_EQ(blend_codec::to_string(static_cast<blend>(99)), "");
}

}  // namespace
