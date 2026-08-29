// SPDX-License-Identifier: Apache-2.0
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include <atp/config/node.hpp>
#include <atp/runtime/config_loader.hpp>
#include <atp/runtime/utf8_path.hpp>

namespace {

class LoaderFiles : public ::testing::Test {
   protected:
    void SetUp() override {
        dir_ = std::filesystem::temp_directory_path() / ::testing::UnitTest::GetInstance()->current_test_info()->name();
        std::filesystem::create_directories(dir_ / "sub");
    }

    std::filesystem::path write(const std::string& relative, const std::string& text) {
        const std::filesystem::path path = dir_ / relative;
        std::ofstream(path) << text;
        return path;
    }

    std::filesystem::path dir_;
};

std::filesystem::path unicode_path(std::initializer_list<char32_t> points, std::string_view suffix) {
    std::u32string name(points);
    for (const char c : suffix) {
        name.push_back(static_cast<char32_t>(c));
    }
    return {name};
}

TEST_F(LoaderFiles, ExpandsNestedIncludesRelativeToIncludingFile) {
    write("sub/leaf.json", R"({"leaf": true})");
    write("sub/mid.json", R"({"mid": {"$include": "leaf.json"}})");
    const auto root = write("root.json", R"({"version": "1.0", "part": {"$include": "sub/mid.json"}})");

    const atp::config::node doc = atp::runtime::load_config(root);
    EXPECT_TRUE(doc.at("part").at("mid").bool_at("leaf"));
}

TEST_F(LoaderFiles, IncludeInsideArrayElement) {
    write("child.json", R"({"group": "g", "modules": []})");
    const auto root = write("root.json", R"({"version": "1.0", "modules": [{"$include": "child.json"}]})");

    const atp::config::node doc = atp::runtime::load_config(root);
    EXPECT_EQ(doc.at("modules")[0].string_at("group"), "g");
}

TEST_F(LoaderFiles, RejectsIncludeCycle) {
    write("a.json", R"({"next": {"$include": "b.json"}})");
    write("b.json", R"({"next": {"$include": "a.json"}})");
    const auto root = write("root.json", R"({"version": "1.0", "x": {"$include": "a.json"}})");

    EXPECT_THROW((void)atp::runtime::load_config(root), atp::runtime::config_error);
}

TEST_F(LoaderFiles, RejectsIncludeWithExtraKeys) {
    write("frag.json", R"({})");
    const auto root = write("root.json", R"({"version": "1.0", "x": {"$include": "frag.json", "extra": 1}})");

    EXPECT_THROW((void)atp::runtime::load_config(root), atp::runtime::config_error);
}

TEST_F(LoaderFiles, RejectsVersionInsideFragment) {
    write("frag.json", R"({"version": "1.0"})");
    const auto root = write("root.json", R"({"version": "1.0", "x": {"$include": "frag.json"}})");

    EXPECT_THROW((void)atp::runtime::load_config(root), atp::runtime::config_error);
}

TEST_F(LoaderFiles, MissingFileAndBadJsonAreConfigErrors) {
    const auto root = write("root.json", R"({"version": "1.0", "x": {"$include": "nowhere.json"}})");
    EXPECT_THROW((void)atp::runtime::load_config(root), atp::runtime::config_error);

    const auto broken = write("broken.json", R"({"version": )");
    EXPECT_THROW((void)atp::runtime::load_config(broken), atp::runtime::config_error);
}

TEST_F(LoaderFiles, ResolvesAnIncludeNamedOutsideAscii) {
    const std::filesystem::path leaf = dir_ / unicode_path({0x043b, 0x0438, 0x0441, 0x0442}, ".json");
    std::ofstream(leaf) << R"({"leaf": true})";
    const std::filesystem::path root = dir_ / "root.json";
    std::ofstream(root) << R"({"version": "1.0", "part": {"$include": ")" << atp::runtime::path_to_utf8(leaf.filename())
                        << R"("}})";

    const atp::config::node doc = atp::runtime::load_config(root);
    EXPECT_TRUE(doc.at("part").bool_at("leaf"))
        << "the include path is UTF-8 in the document, and path(std::string) reads it as the code page";
}

TEST_F(LoaderFiles, NamesAMissingFileInUtf8) {
    const std::filesystem::path missing = dir_ / unicode_path({0x043d, 0x0435, 0x0442}, ".json");
    try {
        (void)atp::runtime::load_config(missing);
        FAIL() << "a missing config file must be a config_error";
    } catch (const atp::runtime::config_error& error) {
        EXPECT_NE(std::string(error.what()).find(atp::runtime::path_to_utf8(missing.filename())), std::string::npos)
            << "path::string() spells the name in the process code page, and every string here is UTF-8";
    }
}

}  // namespace
