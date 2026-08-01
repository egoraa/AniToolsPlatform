#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atp/runtime/config_model.hpp>
#include <atp/runtime/config_validator.hpp>
#include <atp/studio/layout.hpp>

namespace {

atp::runtime::group_node make_group(const char* text) {
    const nlohmann::json proj = nlohmann::json::parse(text);
    EXPECT_TRUE(atp::runtime::validate(proj).empty());
    return atp::runtime::decode(proj).pipeline;
}

TEST(StudioLayout, ChainsFormColumnsByConnectionDirection) {
    const atp::runtime::group_node g = make_group(R"({
        "version": "2.0",
        "pipeline": {
            "modules": [{"module": "a"}, {"module": "b"}, {"module": "c"}],
            "connections": [{"from": "a.out", "to": "b.in"}, {"from": "b.out", "to": "c.in"}]
        }
    })");
    const auto p = atp::studio::auto_layout(g);
    ASSERT_EQ(p.size(), 3u);
    EXPECT_LT(p.at("a").x, p.at("b").x);
    EXPECT_LT(p.at("b").x, p.at("c").x);
}

TEST(StudioLayout, IndependentNodesStackInFirstColumn) {
    const atp::runtime::group_node g = make_group(R"({
        "version": "2.0",
        "pipeline": {"modules": [{"module": "a"}, {"module": "b"}]}
    })");
    const auto p = atp::studio::auto_layout(g);
    EXPECT_EQ(p.at("a").x, p.at("b").x);
    EXPECT_NE(p.at("a").y, p.at("b").y);
}

TEST(StudioLayout, ConnectionCycleDoesNotHang) {
    const atp::runtime::group_node g = make_group(R"({
        "version": "2.0",
        "pipeline": {
            "modules": [{"module": "a"}, {"module": "b"}],
            "connections": [{"from": "a.out", "to": "b.in"}, {"from": "b.out", "to": "a.in"}]
        }
    })");
    const auto p = atp::studio::auto_layout(g);
    EXPECT_EQ(p.size(), 2u);
}

}  // namespace
