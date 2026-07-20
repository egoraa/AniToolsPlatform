#include <filesystem>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include <atp/studio/add_module.hpp>
#include <atp/studio/document.hpp>

namespace {

// Каталог конфига в тестах не создаётся на диске: relative() работает
// лексически (weakly_canonical) и несуществующие пути обрабатывает.
const std::filesystem::path config_dir = "/tmp/atp-cfg";

atp::studio::add_module_request request(const char* factory) {
    atp::studio::add_module_request r;
    r.factory = factory;
    r.plugin = config_dir / "libdemo.so";
    r.config_dir = config_dir;
    return r;
}

TEST(StudioAddModule, UsesFactoryNameWhenFree) {
    atp::studio::document doc = atp::studio::document::create();
    const auto result = atp::studio::add_module(doc, request("counter"));
    EXPECT_EQ(result.name, "counter");
    EXPECT_TRUE(result.warning.empty());
    ASSERT_NE(doc.group_at(""), nullptr);
    EXPECT_EQ(doc.group_at("")->children.size(), 1u);
}

TEST(StudioAddModule, SuffixesNameOnCollision) {
    atp::studio::document doc = atp::studio::document::create();
    EXPECT_EQ(atp::studio::add_module(doc, request("counter")).name, "counter");
    EXPECT_EQ(atp::studio::add_module(doc, request("counter")).name, "counter_2");
    EXPECT_EQ(atp::studio::add_module(doc, request("counter")).name, "counter_3");
}

TEST(StudioAddModule, StoresPositionWhenGiven) {
    atp::studio::document doc = atp::studio::document::create();
    auto r = request("counter");
    r.position = atp::studio::node_position{40.0f, 25.0f};
    const auto result = atp::studio::add_module(doc, r);
    const auto saved = doc.position(result.name);
    ASSERT_TRUE(saved.has_value());
    EXPECT_EQ(*saved, (atp::studio::node_position{40.0f, 25.0f}));
}

TEST(StudioAddModule, LeavesPositionUnsetWithoutRequest) {
    atp::studio::document doc = atp::studio::document::create();
    const auto result = atp::studio::add_module(doc, request("counter"));
    EXPECT_FALSE(doc.position(result.name).has_value());  // канвас возьмёт auto_layout
}

TEST(StudioAddModule, PluginInsideConfigDirBecomesRelative) {
    atp::studio::document doc = atp::studio::document::create();
    const auto result = atp::studio::add_module(doc, request("counter"));
    ASSERT_EQ(doc.config().plugins.size(), 1u);
    EXPECT_EQ(doc.config().plugins[0], "libdemo.so");
    EXPECT_TRUE(result.warning.empty());
}

TEST(StudioAddModule, PluginOutsideConfigDirWalksUp) {
    atp::studio::document doc = atp::studio::document::create();
    auto r = request("counter");
    r.plugin = "/opt/atp/libdemo.so";
    const auto result = atp::studio::add_module(doc, r);
    ASSERT_EQ(doc.config().plugins.size(), 1u);
    EXPECT_EQ(doc.config().plugins[0], "../../opt/atp/libdemo.so");  // /tmp/atp-cfg → / → opt
    EXPECT_TRUE(result.warning.empty());  // относительный путь получился — предупреждать не о чем
}

TEST(StudioAddModule, UnsavedDocumentKeepsAbsolutePluginAndWarns) {
    // Пустой config_dir — документ ещё не сохранён; relative() отдаёт пустой
    // путь, и в конфиг попадает абсолютный. Это то, о чём предупреждаем.
    atp::studio::document doc = atp::studio::document::create();
    auto r = request("counter");
    r.config_dir.clear();
    const auto result = atp::studio::add_module(doc, r);
    ASSERT_EQ(doc.config().plugins.size(), 1u);
    EXPECT_EQ(doc.config().plugins[0], "/tmp/atp-cfg/libdemo.so");
    EXPECT_FALSE(result.warning.empty());
}

TEST(StudioAddModule, SamePluginListedOnce) {
    atp::studio::document doc = atp::studio::document::create();
    atp::studio::add_module(doc, request("counter"));
    atp::studio::add_module(doc, request("printer"));
    EXPECT_EQ(doc.config().plugins.size(), 1u);
}

}  // namespace
