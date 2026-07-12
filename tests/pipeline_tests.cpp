#include <gtest/gtest.h>

#include <atp/pipeline.hpp>

TEST(Pipeline, RootServicesAndContextAreWired) {
    atp::pipeline p;
    EXPECT_EQ(p.root().get_name(), "root");
    EXPECT_EQ(&p.context().services, &p.services());   // контекст собран из служб пайплайна
    p.root().add_group("stage");                       // корень — обычная группа
    EXPECT_NE(p.root().find_group("stage"), nullptr);
}
