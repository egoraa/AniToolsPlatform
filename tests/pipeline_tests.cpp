#include <gtest/gtest.h>

#include <atp/pipeline.hpp>

TEST(Pipeline, RootServicesAndContextAreWired) {
    atp::pipeline p;
    EXPECT_EQ(p.root().get_name(), "root");
    EXPECT_EQ(&p.context().services, &p.services());  // the context is built from the pipeline's services
    p.root().add_group("stage");                      // the root is an ordinary group
    EXPECT_NE(p.root().find_group("stage"), nullptr);
}
