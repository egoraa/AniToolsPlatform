// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include <QString>
#include <QStringList>

#include <atp/hosting/module_registry.hpp>
#include <atp/module.hpp>

#include "model/editor.hpp"

namespace {

using atp::studio::ui::app_state;
using atp::studio::ui::editor_arguments;
using atp::studio::ui::module_source;

class plain_module : public atp::module<atp::ports<>, "plain"> {};

struct harness {
    app_state state;

    harness() {
        state.manager.load_plugin(ATP_TEST_PLUGIN_C);
        state.manager.registry().add<plain_module>();
    }
};

TEST(UiEditor, OnlyAModuleWhosePluginNamedItsFileHasSomethingToOpen) {
    harness h;
    h.state.doc.add_module("", "c_probe", "a");
    h.state.doc.add_module("", "c_bare", "b");
    h.state.doc.add_module("", "plain", "c");
    h.state.doc.add_group("", "g");

    EXPECT_EQ(module_source(h.state, "", "a"), QStringLiteral("c_probe_declared_here.txt"));
    EXPECT_TRUE(module_source(h.state, "", "b").isEmpty()) << "a module compiled into its plugin has no file to open";
    EXPECT_TRUE(module_source(h.state, "", "c").isEmpty()) << "a factory registered by the host came from no plugin";
    EXPECT_TRUE(module_source(h.state, "", "g").isEmpty()) << "a group is not a module";
    EXPECT_TRUE(module_source(h.state, "", "nobody").isEmpty());
    EXPECT_TRUE(module_source(h.state, "nowhere", "a").isEmpty());
}

TEST(UiEditor, TheFileFollowsTheModuleIntoANestedGroup) {
    harness h;
    h.state.doc.add_group("", "g");
    h.state.doc.add_module("g", "c_probe", "a");

    EXPECT_EQ(module_source(h.state, "g", "a"), QStringLiteral("c_probe_declared_here.txt"));
    EXPECT_TRUE(module_source(h.state, "", "a").isEmpty());
}

TEST(UiEditor, DroppingThePluginTakesTheFileWithIt) {
    harness h;
    h.state.doc.add_module("", "c_probe", "a");
    ASSERT_FALSE(module_source(h.state, "", "a").isEmpty());

    ASSERT_TRUE(h.state.manager.unload_plugin(ATP_TEST_PLUGIN_C));
    h.state.invalidate_descriptions();

    EXPECT_TRUE(module_source(h.state, "", "a").isEmpty());
}

TEST(UiEditor, TheFileGoesWhereTheCommandSaysAndIsAppendedOtherwise) {
    EXPECT_EQ(editor_arguments(QStringLiteral("code -g {file}:1"), QStringLiteral("a b.py")),
              (QStringList{QStringLiteral("code"), QStringLiteral("-g"), QStringLiteral("a b.py:1")}));
    EXPECT_EQ(editor_arguments(QStringLiteral("notepad"), QStringLiteral("a b.py")),
              (QStringList{QStringLiteral("notepad"), QStringLiteral("a b.py")}));
}

}  // namespace
