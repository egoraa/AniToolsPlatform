#include <array>
#include <string>

#include <gtest/gtest.h>

#include <atp/group.hpp>
#include <atp/module.hpp>
#include <atp/runtime/property_override.hpp>

namespace {

enum class trace_level { off, brief, full };

}

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
    EXPECT_EQ(o.value, "C:\\a=b\\x.txt");
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

    EXPECT_THROW(atp::runtime::apply_property_override(root, {"ghost.mod", "limit", "1"}), atp::runtime::config_error);
    EXPECT_THROW(atp::runtime::apply_property_override(root, {"ghost", "limit", "1"}), atp::runtime::config_error);
    EXPECT_THROW(atp::runtime::apply_property_override(root, {"mod", "ghost", "1"}), atp::runtime::config_error);
    EXPECT_THROW(atp::runtime::apply_property_override(root, {"mod", "limit", "abc"}), atp::runtime::config_error);
}

}  // namespace
