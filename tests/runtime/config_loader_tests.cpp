// SPDX-License-Identifier: Apache-2.0
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include <atp/runtime/config_loader.hpp>

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

TEST_F(LoaderFiles, ExpandsNestedIncludesRelativeToIncludingFile) {
    write("sub/leaf.json", R"({"leaf": true})");
    write("sub/mid.json", R"({"mid": {"$include": "leaf.json"}})");
    const auto root = write("root.json", R"({"version": "3.0", "part": {"$include": "sub/mid.json"}})");

    const nlohmann::json doc = atp::runtime::load_config(root);
    EXPECT_TRUE(doc.at("part").at("mid").at("leaf").get<bool>());
}

TEST_F(LoaderFiles, IncludeInsideArrayElement) {
    write("child.json", R"({"group": "g", "modules": []})");
    const auto root = write("root.json", R"({"version": "3.0", "modules": [{"$include": "child.json"}]})");

    const nlohmann::json doc = atp::runtime::load_config(root);
    EXPECT_EQ(doc.at("modules").at(0).at("group"), "g");
}

TEST_F(LoaderFiles, RejectsIncludeCycle) {
    write("a.json", R"({"next": {"$include": "b.json"}})");
    write("b.json", R"({"next": {"$include": "a.json"}})");
    const auto root = write("root.json", R"({"version": "3.0", "x": {"$include": "a.json"}})");

    EXPECT_THROW((void)atp::runtime::load_config(root), atp::runtime::config_error);
}

TEST_F(LoaderFiles, RejectsIncludeWithExtraKeys) {
    write("frag.json", R"({})");
    const auto root = write("root.json", R"({"version": "3.0", "x": {"$include": "frag.json", "extra": 1}})");

    EXPECT_THROW((void)atp::runtime::load_config(root), atp::runtime::config_error);
}

TEST_F(LoaderFiles, RejectsVersionInsideFragment) {
    write("frag.json", R"({"version": "3.0"})");
    const auto root = write("root.json", R"({"version": "3.0", "x": {"$include": "frag.json"}})");

    EXPECT_THROW((void)atp::runtime::load_config(root), atp::runtime::config_error);
}

TEST_F(LoaderFiles, MissingFileAndBadJsonAreConfigErrors) {
    const auto root = write("root.json", R"({"version": "3.0", "x": {"$include": "nowhere.json"}})");
    EXPECT_THROW((void)atp::runtime::load_config(root), atp::runtime::config_error);

    const auto broken = write("broken.json", R"({"version": )");
    EXPECT_THROW((void)atp::runtime::load_config(broken), atp::runtime::config_error);
}

}  // namespace
