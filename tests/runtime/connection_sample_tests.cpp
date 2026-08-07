// SPDX-License-Identifier: Apache-2.0
#include <vector>

#include <gtest/gtest.h>

#include <atp/group.hpp>
#include <atp/module.hpp>
#include <atp/runtime/connection_sample.hpp>

namespace {

struct number_outputs : atp::io::outputs {
    atp::io::output<int>& number = make<atp::io::output<int>>("number");
};
struct number_inputs : atp::io::inputs {
    atp::io::input<int>& number = make<atp::io::input<int>>("number");
};

class sample_source : public atp::module<atp::io::ports<atp::io::inputs, number_outputs>, "sample_source"> {
   public:
    void send(int value) {
        outputs().number(value);
    }
};

class sample_sink : public atp::module<atp::io::ports<number_inputs>, "sample_sink"> {};

TEST(ConnectionSample, NumbersConnectionsPerGroupAndNamesNestedGroupsByPath) {
    atp::group root("root");
    sample_source& src = root.make<sample_source>("src");
    root.make<sample_sink>("dst");
    root.connect("src.number", "dst.number");

    atp::group& stage = root.add_group("stage");
    stage.make<sample_source>("inner_src");
    stage.make<sample_sink>("inner_dst");
    stage.connect("inner_src.number", "inner_dst.number");

    src.send(7);

    const std::vector<atp::runtime::connection_sample> samples = atp::runtime::sample_connections(root);

    ASSERT_EQ(samples.size(), 2u);
    EXPECT_EQ(samples[0].group_path, "");
    EXPECT_EQ(samples[0].index, 0u);
    EXPECT_EQ(samples[0].writes, 1u);
    EXPECT_EQ(samples[1].group_path, "stage");
    EXPECT_EQ(samples[1].index, 0u);
    EXPECT_EQ(samples[1].writes, 0u);
}

}  // namespace
