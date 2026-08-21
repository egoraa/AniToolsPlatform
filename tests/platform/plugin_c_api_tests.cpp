// SPDX-License-Identifier: Apache-2.0
#include <cstddef>

#include <gtest/gtest.h>

#include <atp/plugin_c.h>

namespace {

TEST(PluginCApi, TheConfigAccessorsAreAskedForByTheirOwnOffset) {
    atp_api api{};

    api.struct_size = offsetof(atp_api, config_root);
    EXPECT_FALSE(atp_api_has_config(&api)) << "a host stopping before config_root carries none of them";

    api.struct_size = offsetof(atp_api, config_value_of);
    EXPECT_FALSE(atp_api_has_config(&api)) << "the last of them counts only once it is whole";

    api.struct_size = offsetof(atp_api, config_value_of) + sizeof(api.config_value_of);
    EXPECT_TRUE(atp_api_has_config(&api));
}

TEST(PluginCApi, AHostLargerThanThisHeaderStillCarriesTheConfigAccessors) {
    atp_api api{};
    api.struct_size = sizeof(atp_api) + 64;
    EXPECT_TRUE(atp_api_has_config(&api))
        << "the question is about these fields alone, so callbacks appended later cannot answer it";
    EXPECT_TRUE(atp_api_has_config_text(&api));
}

TEST(PluginCApi, TheTextAccessorsAreAskedForByAnOffsetOfTheirOwn) {
    atp_api api{};

    api.struct_size = offsetof(atp_api, config_value_of) + sizeof(api.config_value_of);
    EXPECT_TRUE(atp_api_has_config(&api));
    EXPECT_FALSE(atp_api_has_config_text(&api)) << "a host with the config and without the text has to say so";

    api.struct_size = offsetof(atp_api, config_is_opaque);
    EXPECT_FALSE(atp_api_has_config_text(&api)) << "the last of them counts only once it is whole";

    api.struct_size = offsetof(atp_api, config_is_opaque) + sizeof(api.config_is_opaque);
    EXPECT_TRUE(atp_api_has_config_text(&api));
}

}  // namespace
