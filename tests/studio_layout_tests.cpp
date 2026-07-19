#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atp/app/config_model.hpp>
#include <atp/app/config_validator.hpp>
#include <atp/studio/layout.hpp>

namespace {

atp::app::group_node make_group(const char* text) {
    const nlohmann::json doc = nlohmann::json::parse(text);
    EXPECT_TRUE(atp::app::validate(doc).empty());
    return atp::app::decode(doc).pipeline;
}

TEST(StudioLayout, ChainsFormColumnsByConnectionDirection) {
    const atp::app::group_node g = make_group(R"({
        "version": "1.0",
        "pipeline": {
            "children": [{"module": "a"}, {"module": "b"}, {"module": "c"}],
            "connections": [{"from": "a.out", "to": "b.in"}, {"from": "b.out", "to": "c.in"}]
        }
    })");
    const auto p = atp::studio::auto_layout(g);
    ASSERT_EQ(p.size(), 3u);
    EXPECT_LT(p.at("a").x, p.at("b").x);  // цепочка растёт вправо
    EXPECT_LT(p.at("b").x, p.at("c").x);
}

TEST(StudioLayout, IndependentNodesStackInFirstColumn) {
    const atp::app::group_node g = make_group(R"({
        "version": "1.0",
        "pipeline": {"children": [{"module": "a"}, {"module": "b"}]}
    })");
    const auto p = atp::studio::auto_layout(g);
    EXPECT_EQ(p.at("a").x, p.at("b").x);  // один слой
    EXPECT_NE(p.at("a").y, p.at("b").y);  // разные строки — не внахлёст
}

TEST(StudioLayout, ConnectionCycleDoesNotHang) {
    const atp::app::group_node g = make_group(R"({
        "version": "1.0",
        "pipeline": {
            "children": [{"module": "a"}, {"module": "b"}],
            "connections": [{"from": "a.out", "to": "b.in"}, {"from": "b.out", "to": "a.in"}]
        }
    })");
    const auto p = atp::studio::auto_layout(g);  // завершилась — уже успех
    EXPECT_EQ(p.size(), 2u);
}

}  // namespace
