// SPDX-License-Identifier: Apache-2.0
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <atp/studio/settings.hpp>

namespace {

std::filesystem::path temp_file(const std::string& name) {
    const auto dir =
        std::filesystem::temp_directory_path() / ::testing::UnitTest::GetInstance()->current_test_info()->name();
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

TEST(StudioSettings, RoundTripsRecentProjects) {
    const auto file = temp_file("settings.json");
    atp::studio::studio_settings s;
    s.recent_projects = {"C:/a.json", "C:/b.json"};
    atp::studio::save_settings(s, file);

    EXPECT_EQ(atp::studio::load_settings(file).recent_projects, s.recent_projects);
}

TEST(StudioSettings, LoadSkipsNonStringRecentEntries) {
    const auto file = temp_file("settings.json");
    std::ofstream(file) << R"({"recent_projects": ["C:/a.json", 5, null]})";

    EXPECT_EQ(atp::studio::load_settings(file).recent_projects, std::vector<std::string>{"C:/a.json"});
}

TEST(StudioSettings, RoundTripsTheme) {
    const auto file = temp_file("settings.json");
    for (const atp::studio::app_theme theme :
         {atp::studio::app_theme::system, atp::studio::app_theme::light, atp::studio::app_theme::dark}) {
        atp::studio::studio_settings s;
        s.theme = theme;
        atp::studio::save_settings(s, file);

        EXPECT_EQ(atp::studio::load_settings(file).theme, theme);
    }
}

TEST(StudioSettings, ThemeDefaultsToSystem) {
    EXPECT_EQ(atp::studio::studio_settings{}.theme, atp::studio::app_theme::system);

    const auto file = temp_file("settings.json");
    std::ofstream(file) << R"({"search_dirs": []})";
    EXPECT_EQ(atp::studio::load_settings(file).theme, atp::studio::app_theme::system);
}

TEST(StudioSettings, UnreadableThemeYieldsSystem) {
    const auto unknown = temp_file("unknown.json");
    std::ofstream(unknown) << R"({"theme": "chartreuse"})";
    EXPECT_EQ(atp::studio::load_settings(unknown).theme, atp::studio::app_theme::system);

    const auto wrong_type = temp_file("wrong_type.json");
    std::ofstream(wrong_type) << R"({"theme": 3})";
    EXPECT_EQ(atp::studio::load_settings(wrong_type).theme, atp::studio::app_theme::system);
}

TEST(StudioSettings, RoundTripsStyle) {
    const auto file = temp_file("settings.json");
    atp::studio::studio_settings s;
    s.style = "Fusion";
    atp::studio::save_settings(s, file);

    EXPECT_EQ(atp::studio::load_settings(file).style, "Fusion");
}

TEST(StudioSettings, StyleDefaultsToEmpty) {
    EXPECT_TRUE(atp::studio::studio_settings{}.style.empty());

    const auto file = temp_file("settings.json");
    std::ofstream(file) << R"({"search_dirs": []})";
    EXPECT_TRUE(atp::studio::load_settings(file).style.empty());
}

TEST(StudioSettings, StyleIsCarriedVerbatim) {
    const auto file = temp_file("settings.json");
    std::ofstream(file) << R"({"style": "Breeze"})";

    EXPECT_EQ(atp::studio::load_settings(file).style, "Breeze");
}

TEST(StudioSettings, NonStringStyleIsIgnored) {
    const auto file = temp_file("settings.json");
    std::ofstream(file) << R"({"style": 3, "recent_projects": ["C:/a.json"]})";

    const atp::studio::studio_settings loaded = atp::studio::load_settings(file);
    EXPECT_TRUE(loaded.style.empty());
    EXPECT_EQ(loaded.recent_projects, std::vector<std::string>{"C:/a.json"});
}

TEST(StudioSettings, RoundTripsWindowLayout) {
    const auto file = temp_file("settings.json");
    atp::studio::studio_settings s;
    s.window_geometry = "AdnQywADAAAAAAAA";
    s.window_state = "AAAA/wAAAAD9AAAA";
    atp::studio::save_settings(s, file);

    const atp::studio::studio_settings loaded = atp::studio::load_settings(file);
    EXPECT_EQ(loaded.window_geometry, s.window_geometry);
    EXPECT_EQ(loaded.window_state, s.window_state);
}

TEST(StudioSettings, WindowLayoutDefaultsToEmpty) {
    EXPECT_TRUE(atp::studio::studio_settings{}.window_geometry.empty());
    EXPECT_TRUE(atp::studio::studio_settings{}.window_state.empty());

    const auto file = temp_file("settings.json");
    std::ofstream(file) << R"({"search_dirs": []})";
    const atp::studio::studio_settings loaded = atp::studio::load_settings(file);
    EXPECT_TRUE(loaded.window_geometry.empty());
    EXPECT_TRUE(loaded.window_state.empty());
}

TEST(StudioSettings, NonStringWindowLayoutIsIgnored) {
    const auto file = temp_file("settings.json");
    std::ofstream(file) << R"({"window_geometry": 7, "window_state": null, "recent_projects": ["C:/a.json"]})";

    const atp::studio::studio_settings loaded = atp::studio::load_settings(file);
    EXPECT_TRUE(loaded.window_geometry.empty());
    EXPECT_TRUE(loaded.window_state.empty());
    EXPECT_EQ(loaded.recent_projects, std::vector<std::string>{"C:/a.json"});
}

TEST(ThemeName, MapsEveryValueBothWays) {
    for (const atp::studio::app_theme theme :
         {atp::studio::app_theme::system, atp::studio::app_theme::light, atp::studio::app_theme::dark}) {
        EXPECT_EQ(atp::studio::theme_from_name(atp::studio::theme_name(theme)), theme);
    }
    EXPECT_FALSE(atp::studio::theme_from_name("nope").has_value());
}

TEST(NoteRecent, PutsNewestFirst) {
    atp::studio::studio_settings s;
    atp::studio::note_recent(s, "a.json");
    atp::studio::note_recent(s, "b.json");

    const auto a = std::filesystem::absolute("a.json").lexically_normal().string();
    const auto b = std::filesystem::absolute("b.json").lexically_normal().string();
    EXPECT_EQ(s.recent_projects, (std::vector<std::string>{b, a}));
}

TEST(NoteRecent, MovesDuplicateToFrontWithoutGrowth) {
    atp::studio::studio_settings s;
    atp::studio::note_recent(s, "a.json");
    atp::studio::note_recent(s, "b.json");
    atp::studio::note_recent(s, "a.json");

    const auto a = std::filesystem::absolute("a.json").lexically_normal().string();
    const auto b = std::filesystem::absolute("b.json").lexically_normal().string();
    EXPECT_EQ(s.recent_projects, (std::vector<std::string>{a, b}));
}

TEST(NoteRecent, CapsAtLimit) {
    atp::studio::studio_settings s;
    for (int i = 0; i < 12; ++i) {
        atp::studio::note_recent(s, "p" + std::to_string(i) + ".json");
    }

    ASSERT_EQ(s.recent_projects.size(), atp::studio::recent_limit);
    EXPECT_EQ(s.recent_projects.front(), std::filesystem::absolute("p11.json").lexically_normal().string());
    EXPECT_EQ(s.recent_projects.back(), std::filesystem::absolute("p2.json").lexically_normal().string());
}

TEST(NoteRecent, NormalizesRelativePath) {
    atp::studio::studio_settings s;
    atp::studio::note_recent(s, "sub/../x.json");

    EXPECT_EQ(s.recent_projects.front(), (std::filesystem::current_path() / "x.json").lexically_normal().string());
}

}  // namespace
