#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include <atp/studio/settings.hpp>

namespace {

std::filesystem::path temp_file(const std::string& name) {
    const auto dir =
        std::filesystem::path(::testing::TempDir()) / ::testing::UnitTest::GetInstance()->current_test_info()->name();
    std::filesystem::create_directories(dir);
    return dir / name;
}

TEST(StudioSettings, RoundTripsSearchDirs) {
    const auto file = temp_file("settings.json");
    atp::studio::studio_settings s;
    s.search_dirs = {"C:/plugins", "../more"};
    atp::studio::save_settings(s, file);

    const atp::studio::studio_settings loaded = atp::studio::load_settings(file);
    EXPECT_EQ(loaded.search_dirs, s.search_dirs);
}

TEST(StudioSettings, MissingOrBrokenFileYieldsDefaults) {
    EXPECT_TRUE(atp::studio::load_settings(temp_file("nowhere.json")).search_dirs.empty());

    const auto broken = temp_file("broken.json");
    std::ofstream(broken) << "{not json";
    EXPECT_TRUE(atp::studio::load_settings(broken).search_dirs.empty());
}

TEST(StudioSettings, SaveCreatesParentDirectories) {
    const auto file = temp_file("deep") / "nested" / "settings.json";
    atp::studio::save_settings({}, file);
    EXPECT_TRUE(std::filesystem::exists(file));
}

}  // namespace
