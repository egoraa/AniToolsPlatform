// SPDX-License-Identifier: Apache-2.0
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include <atp/studio/languages.hpp>
#include <atp/studio/script_modules.hpp>

namespace {

std::filesystem::path fresh_dir(const char* leaf) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "atp_python_scripts_tests" / leaf;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

TEST(PythonLanguage, ANameIsUsableWhenItSurvivesBothAsAnAttributeAndAsAClass) {
    EXPECT_TRUE(atp::studio::python_language.name_valid("py_a"));
    EXPECT_TRUE(atp::studio::python_language.name_valid("_x"));
    EXPECT_TRUE(atp::studio::python_language.name_valid("A1"));
    EXPECT_TRUE(atp::studio::python_language.name_valid("py_"));

    EXPECT_FALSE(atp::studio::python_language.name_valid(""));
    EXPECT_FALSE(atp::studio::python_language.name_valid("1x"));
    EXPECT_FALSE(atp::studio::python_language.name_valid("py-a"));
    EXPECT_FALSE(atp::studio::python_language.name_valid("py a"));
    EXPECT_FALSE(atp::studio::python_language.name_valid("py.a"));
    EXPECT_FALSE(atp::studio::python_language.name_valid("___"));
    EXPECT_FALSE(atp::studio::python_language.name_valid("_1"));

    EXPECT_FALSE(atp::studio::python_language.name_valid("atp"))
        << "atp.py beside the atp package replaces it in sys.modules and every later script stops importing";
    EXPECT_TRUE(atp::studio::python_language.name_valid("atp_thing"));
}

TEST(ScriptModules, OnlyAnOlderBridgeCopyCountsAsStale) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "atp_bridge_staleness";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::filesystem::path copy = dir / "copy.dll";
    const std::filesystem::path platform = dir / "platform.dll";
    std::ofstream(copy) << "\n";
    std::ofstream(platform) << "\n";

    std::filesystem::last_write_time(copy, std::filesystem::last_write_time(platform) - std::chrono::hours(1));
    EXPECT_TRUE(atp::studio::bridge_copy_is_stale(copy, platform));

    std::filesystem::last_write_time(copy, std::filesystem::last_write_time(platform) + std::chrono::hours(1));
    EXPECT_FALSE(atp::studio::bridge_copy_is_stale(copy, platform));

    EXPECT_FALSE(atp::studio::bridge_copy_is_stale(platform, platform)) << "one file is never older than itself";
    EXPECT_FALSE(atp::studio::bridge_copy_is_stale(dir / "absent.dll", platform))
        << "a time that could not be read is not evidence of age";

    std::filesystem::remove_all(dir);
}

TEST(PythonLanguage, TheClassNameIsPascalCaseWithoutTheLeadingPy) {
    EXPECT_EQ(atp::studio::python_class_name("py_my_thing"), "MyThing");
    EXPECT_EQ(atp::studio::python_class_name("my_thing"), "MyThing");
    EXPECT_EQ(atp::studio::python_class_name("py_"), "Py");
    EXPECT_EQ(atp::studio::python_class_name("x"), "X");
}

TEST(ScriptModules, TheFileNameKeepsTheModuleNameWholeSoStemsStayDistinct) {
    EXPECT_EQ(atp::studio::script_file_name("py_my_thing", atp::studio::python_language), "py_my_thing.py");
}

TEST(PythonLanguage, TheSkeletonDeclaresTheNameTheClassAndThePortsItPromises) {
    const std::string text = atp::studio::render_python_module("py_my_thing");
    EXPECT_NE(text.find("class MyThing(atp.Module):"), std::string::npos);
    EXPECT_NE(text.find("name = \"py_my_thing\""), std::string::npos);
    EXPECT_NE(text.find("value = atp.Input(atp.i32)"), std::string::npos);
    EXPECT_NE(text.find("result = atp.Output(atp.i32)"), std::string::npos);
    EXPECT_NE(text.find("factor = atp.Property(atp.i32, 2)"), std::string::npos);
    EXPECT_NE(text.find("import atp"), std::string::npos);
    EXPECT_EQ(text.find("@NAME@"), std::string::npos);
    EXPECT_EQ(text.find("@CLASS@"), std::string::npos);
}

TEST(PythonLanguage, TheSkeletonShowsHowTheConfigArrives) {
    const std::string text = atp::studio::render_python_module("py_my_thing");
    EXPECT_NE(text.find("def __init__(self):"), std::string::npos)
        << "the constructor is the only place the config may be read, so the skeleton has to have one";
    EXPECT_NE(text.find("self.config"), std::string::npos)
        << "an author who never sees the attribute cannot discover the channel: it is set by the bridge, "
           "so nothing in the class body hints at it";
}

TEST(ScriptModules, CreatingWritesTheSkeletonAndRefusesToTouchAnExistingFile) {
    const std::filesystem::path dir = fresh_dir("create");

    const std::filesystem::path file =
        atp::studio::create_script_module(dir, "py_my_thing", atp::studio::python_language);
    EXPECT_EQ(file, dir / "py_my_thing.py");
    ASSERT_TRUE(std::filesystem::exists(file));

    std::ostringstream read;
    {
        std::ifstream in(file);
        read << in.rdbuf();
    }
    EXPECT_EQ(read.str(), atp::studio::render_python_module("py_my_thing"));

    EXPECT_THROW((void)atp::studio::create_script_module(dir, "py_my_thing", atp::studio::python_language),
                 std::runtime_error);
    EXPECT_THROW((void)atp::studio::create_script_module(dir, "1bad", atp::studio::python_language),
                 std::runtime_error);

    std::filesystem::remove_all(dir);
}

TEST(ScriptModules, ScriptDirectoriesAreDerivedFromTheSearchDirectories) {
    const std::filesystem::path root = fresh_dir("derive");
    std::filesystem::create_directories(root / "with_scripts" / "python");
    std::filesystem::create_directories(root / "plain_plugins");

    const std::vector<std::filesystem::path> dirs{root / "plain_plugins", root / "with_scripts", root / "gone"};
    const std::vector<std::string> derived = atp::studio::derive_script_dirs(dirs, atp::studio::python_language);

    ASSERT_EQ(derived.size(), 1u);
    EXPECT_EQ(derived.front(), (root / "with_scripts" / "python").string());

    const std::optional<std::filesystem::path> offered =
        atp::studio::last_script_folder(dirs, atp::studio::python_language);
    ASSERT_TRUE(offered.has_value());
    EXPECT_EQ(*offered, root / "with_scripts");
    EXPECT_FALSE(atp::studio::last_script_folder({root / "plain_plugins"}, atp::studio::python_language).has_value());

    std::filesystem::remove_all(root);
}

TEST(ScriptModules, AModuleFolderKeepsItsScriptsBesideTheAtpPackage) {
    EXPECT_EQ(atp::studio::scripts_dir("c:/mine", atp::studio::python_language),
              std::filesystem::path("c:/mine") / "python");
    EXPECT_EQ(atp::studio::bridge_filename(atp::studio::python_language),
              std::string("atp_python_bridge") + std::string(atp::runtime::plugin_extension));
}

TEST(ScriptModules, ProvisioningCopiesTheBridgeAndThePackageOnlyWhenTheyAreMissing) {
    const std::filesystem::path root = fresh_dir("provision");
    const std::filesystem::path fake_plugins = root / "from";
    std::filesystem::create_directories(fake_plugins / "python" / "atp");
    std::ofstream(fake_plugins / atp::studio::bridge_filename(atp::studio::python_language)) << "not really a library";
    std::ofstream(fake_plugins / "python" / "atp" / "__init__.py") << "# package\n";

    atp::studio::bridge_source source;
    source.bridge = fake_plugins / atp::studio::bridge_filename(atp::studio::python_language);
    source.package = fake_plugins / "python" / "atp";

    const std::filesystem::path folder = root / "modules";
    const atp::studio::folder_setup first = atp::studio::provision_folder(folder, source, atp::studio::python_language);
    EXPECT_EQ(first.scripts_dir, folder / "python");
    EXPECT_TRUE(first.bridge_copied);
    EXPECT_TRUE(first.package_copied);
    EXPECT_TRUE(std::filesystem::exists(folder / atp::studio::bridge_filename(atp::studio::python_language)));
    EXPECT_TRUE(std::filesystem::exists(folder / "python" / "atp" / "__init__.py"));

    const atp::studio::folder_setup again = atp::studio::provision_folder(folder, source, atp::studio::python_language);
    EXPECT_FALSE(again.bridge_copied);
    EXPECT_FALSE(again.package_copied);

    std::filesystem::remove_all(root);
}

TEST(ScriptModules, AStalePackageIsRefreshedWhileAStaleBridgeIsOnlyReported) {
    const std::filesystem::path root = fresh_dir("refresh");
    const std::filesystem::path from = root / "from";
    std::filesystem::create_directories(from / "python" / "atp");
    std::ofstream(from / atp::studio::bridge_filename(atp::studio::python_language)) << "new library";
    std::ofstream(from / "python" / "atp" / "__init__.py") << "# new package\n";

    atp::studio::bridge_source source;
    source.bridge = from / atp::studio::bridge_filename(atp::studio::python_language);
    source.package = from / "python" / "atp";

    const std::filesystem::path folder = root / "modules";
    std::filesystem::create_directories(folder / "python" / "atp");
    std::ofstream(folder / atp::studio::bridge_filename(atp::studio::python_language)) << "old library";
    std::ofstream(folder / "python" / "atp" / "__init__.py") << "# old package\n";
    const auto long_ago =
        std::filesystem::last_write_time(from / "python" / "atp" / "__init__.py") - std::chrono::hours(48);
    std::filesystem::last_write_time(folder / "python" / "atp" / "__init__.py", long_ago);
    std::filesystem::last_write_time(folder / atp::studio::bridge_filename(atp::studio::python_language), long_ago);
    std::filesystem::create_directories(folder / "python" / "atp" / "__pycache__");
    std::ofstream(folder / "python" / "atp" / "__pycache__" / "__init__.cpython-311.pyc") << "fresh bytecode";

    const atp::studio::folder_setup done = atp::studio::provision_folder(folder, source, atp::studio::python_language);

    EXPECT_FALSE(done.bridge_copied);
    EXPECT_FALSE(done.package_copied);
    EXPECT_TRUE(done.package_refreshed);
    EXPECT_TRUE(done.bridge_stale);

    std::ostringstream package;
    {
        std::ifstream in(folder / "python" / "atp" / "__init__.py");
        package << in.rdbuf();
    }
    EXPECT_EQ(package.str(), "# new package\n");

    std::ostringstream bridge;
    {
        std::ifstream in(folder / atp::studio::bridge_filename(atp::studio::python_language));
        bridge << in.rdbuf();
    }
    EXPECT_EQ(bridge.str(), "old library");

    std::filesystem::remove_all(root);
}

TEST(ScriptModules, AFreshPackageIsLeftAlone) {
    const std::filesystem::path root = fresh_dir("fresh");
    const std::filesystem::path from = root / "from";
    std::filesystem::create_directories(from / "python" / "atp");
    std::ofstream(from / atp::studio::bridge_filename(atp::studio::python_language)) << "library";
    std::ofstream(from / "python" / "atp" / "__init__.py") << "# package\n";

    atp::studio::bridge_source source;
    source.bridge = from / atp::studio::bridge_filename(atp::studio::python_language);
    source.package = from / "python" / "atp";

    const std::filesystem::path folder = root / "modules";
    (void)atp::studio::provision_folder(folder, source, atp::studio::python_language);
    const atp::studio::folder_setup again = atp::studio::provision_folder(folder, source, atp::studio::python_language);

    EXPECT_FALSE(again.package_copied);
    EXPECT_FALSE(again.package_refreshed);
    EXPECT_FALSE(again.bridge_stale);

    std::filesystem::remove_all(root);
}

TEST(ScriptModules, ProvisioningWithoutASourceStillMakesTheScriptsDirectory) {
    const std::filesystem::path root = fresh_dir("no_source");

    const atp::studio::folder_setup done =
        atp::studio::provision_folder(root / "modules", atp::studio::bridge_source{}, atp::studio::python_language);

    EXPECT_TRUE(std::filesystem::exists(done.scripts_dir));
    EXPECT_FALSE(done.bridge_copied);
    EXPECT_FALSE(done.package_copied);
    EXPECT_FALSE(
        std::filesystem::exists(root / "modules" / atp::studio::bridge_filename(atp::studio::python_language)));

    std::filesystem::remove_all(root);
}

TEST(ScriptModules, TheSourceSaysWhereItLookedWhenNothingWasFound) {
    const std::filesystem::path root = fresh_dir("source_search");
    atp::studio::module_manager empty;

    const atp::studio::bridge_source source =
        atp::studio::find_bridge_source(empty, root, atp::studio::python_language);

    EXPECT_FALSE(source.found());
    ASSERT_EQ(source.searched.size(), 2u);
    EXPECT_EQ(source.searched.at(0), root / "plugins" / atp::studio::bridge_filename(atp::studio::python_language));
    EXPECT_EQ(source.searched.at(1), root / atp::studio::bridge_filename(atp::studio::python_language));

    std::filesystem::remove_all(root);
}

TEST(ScriptModules, TheInstallationOutranksALoadedBridgeAsACopySource) {
    const std::filesystem::path root = fresh_dir("source_order");
    std::filesystem::create_directories(root / "plugins" / "python" / "atp");
    std::ofstream(root / "plugins" / atp::studio::bridge_filename(atp::studio::python_language))
        << "the platform's own";
    std::ofstream(root / "plugins" / "python" / "atp" / "__init__.py") << "# platform\n";

    atp::studio::module_manager manager;
    manager.add_search_dir(root / "elsewhere");
    manager.rescan();

    const atp::studio::bridge_source source =
        atp::studio::find_bridge_source(manager, root, atp::studio::python_language);

    ASSERT_TRUE(source.found());
    EXPECT_EQ(source.bridge, root / "plugins" / atp::studio::bridge_filename(atp::studio::python_language));
    EXPECT_EQ(source.searched.front(), root / "plugins" / atp::studio::bridge_filename(atp::studio::python_language));

    std::filesystem::remove_all(root);
}

TEST(ScriptModules, ProvisioningRefusesToCopyAFolderOntoItself) {
    const std::filesystem::path root = fresh_dir("self_copy");
    std::filesystem::create_directories(root / "python" / "atp");
    std::ofstream(root / atp::studio::bridge_filename(atp::studio::python_language)) << "library";
    std::ofstream(root / "python" / "atp" / "__init__.py") << "# package\n";

    atp::studio::bridge_source source;
    source.bridge = root / atp::studio::bridge_filename(atp::studio::python_language);
    source.package = root / "python" / "atp";

    const atp::studio::folder_setup done = atp::studio::provision_folder(root, source, atp::studio::python_language);

    EXPECT_FALSE(done.bridge_copied);
    EXPECT_FALSE(done.package_copied);
    EXPECT_FALSE(done.package_refreshed);
    EXPECT_FALSE(done.bridge_stale);
    EXPECT_EQ(done.scripts_dir, root / "python");

    std::filesystem::remove_all(root);
}

TEST(ScriptModules, ABridgeBesideTheExecutableIsAcceptedOnlyWithItsPackage) {
    const std::filesystem::path root = fresh_dir("source_beside");
    atp::studio::module_manager empty;
    std::ofstream(root / atp::studio::bridge_filename(atp::studio::python_language)) << "not really a library";

    EXPECT_FALSE(atp::studio::find_bridge_source(empty, root, atp::studio::python_language).found());

    std::filesystem::create_directories(root / "python" / "atp");
    const atp::studio::bridge_source source =
        atp::studio::find_bridge_source(empty, root, atp::studio::python_language);
    EXPECT_TRUE(source.found());
    EXPECT_EQ(source.bridge, root / atp::studio::bridge_filename(atp::studio::python_language));
    EXPECT_EQ(source.package, root / "python" / "atp");

    std::filesystem::remove_all(root);
}

TEST(ScriptModules, TheScanPathPutsTheStudioDirectoriesBeforeTheInheritedTail) {
    const char sep = atp::studio::script_path_separator;
    const std::string expected = std::string("a") + sep + "b" + sep + "kept";
    EXPECT_EQ(atp::studio::compose_script_path({"a", "b"}, "kept"), expected);
}

TEST(ScriptModules, TheScanPathDropsDuplicatesAndNeverDanglesASeparator) {
    const char sep = atp::studio::script_path_separator;
    EXPECT_EQ(atp::studio::compose_script_path({"a", "a"}, ""), "a");
    EXPECT_EQ(atp::studio::compose_script_path({"a"}, "a"), "a");
    EXPECT_EQ(atp::studio::compose_script_path({}, "kept"), "kept");
    EXPECT_EQ(atp::studio::compose_script_path({}, ""), "");
    const std::string tail = std::string(1, sep) + "b" + sep + sep + "c" + sep;
    EXPECT_EQ(atp::studio::compose_script_path({"a"}, tail), std::string("a") + sep + "b" + sep + "c");
}

TEST(ScriptModules, ApplyingTheScanPathIsWhatTheBridgeWouldRead) {
    atp::studio::apply_script_path({"one", "two"}, "tail", atp::studio::python_language);
    EXPECT_EQ(atp::studio::inherited_script_path(atp::studio::python_language),
              atp::studio::compose_script_path({"one", "two"}, "tail"));

    atp::studio::apply_script_path({}, "", atp::studio::python_language);
    EXPECT_TRUE(atp::studio::inherited_script_path(atp::studio::python_language).empty());
}

}  // namespace
