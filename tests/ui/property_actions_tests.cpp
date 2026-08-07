// SPDX-License-Identifier: Apache-2.0
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <QString>

#include <atp/module.hpp>
#include <atp/module_registry.hpp>

#include "model/property_actions.hpp"

namespace {

using atp::studio::ui::app_state;
using atp::studio::ui::copy_properties;
using atp::studio::ui::paste_properties;
using atp::studio::ui::ui_callbacks;

struct gain_props : atp::io::properties {
    atp::io::property<int>& step = make<atp::io::property<int>>("step", 1);
    atp::io::property<bool>& loud = make<atp::io::property<bool>>("loud", false);
};
class gain_module : public atp::module<atp::io::ports<atp::io::inputs, atp::io::outputs, gain_props>, "gain"> {};

struct plain_props : atp::io::properties {
    atp::io::property<int>& step = make<atp::io::property<int>>("step", 1);
};
class plain_module : public atp::module<atp::io::ports<atp::io::inputs, atp::io::outputs, plain_props>, "plain"> {};

struct wide_props : atp::io::properties {
    atp::io::property<int>& channels = make<atp::io::property<int>>("channels", 2, atp::io::allowed(1, 2, 6));
    atp::io::property<std::string>& label = make<atp::io::property<std::string>>("label", "x");
    atp::io::property<int>& step = make<atp::io::property<int>>("step", 1);
};
class wide_module : public atp::module<atp::io::ports<atp::io::inputs, atp::io::outputs, wide_props>, "wide"> {};

struct narrow_props : atp::io::properties {
    atp::io::property<int>& channels = make<atp::io::property<int>>("channels", 1, atp::io::allowed(1, 2));
    atp::io::property<int>& label = make<atp::io::property<int>>("label", 0);
    atp::io::property<int>& step = make<atp::io::property<int>>("step", 1, atp::io::transient);
};
class narrow_module : public atp::module<atp::io::ports<atp::io::inputs, atp::io::outputs, narrow_props>, "narrow"> {};

struct harness {
    app_state state;
    ui_callbacks callbacks;
    std::vector<std::string> log;

    harness() {
        callbacks.project_changed = [] {};
        callbacks.error = [this](const QString& text) { log.push_back(text.toStdString()); };
        callbacks.selection_changed = [] {};
        state.manager.registry().add<gain_module>();
        state.manager.registry().add<plain_module>();
        state.manager.registry().add<wide_module>();
        state.manager.registry().add<narrow_module>();
    }

    [[nodiscard]] const atp::runtime::module_node& module_at(const std::string& name) const {
        for (const atp::runtime::child_node& c : state.doc.group_at("")->modules) {
            if (c.module && c.module->name == name) {
                return *c.module;
            }
        }
        throw std::runtime_error("no module " + name);
    }
};

TEST(UiPropertyActions, CopyTakesOnlyWhatTheProjectStatesExplicitly) {
    harness h;
    h.state.doc.add_module("", "gain", "a");
    h.state.doc.set_property("", "a", "step", 5);

    EXPECT_TRUE(copy_properties(h.state, h.callbacks, "", "a"));

    EXPECT_EQ(h.state.clip_properties.factory, "gain");
    ASSERT_EQ(h.state.clip_properties.values.size(), 1u);
    EXPECT_EQ(h.state.clip_properties.values.front().first, "step");
    EXPECT_EQ(h.state.clip_properties.values.front().second, 5);
}

TEST(UiPropertyActions, PasteHandsTheValuesToAnotherModule) {
    harness h;
    h.state.doc.add_module("", "gain", "a");
    h.state.doc.set_property("", "a", "step", 5);
    h.state.doc.set_property("", "a", "loud", true);
    h.state.doc.add_module("", "gain", "b");
    ASSERT_TRUE(copy_properties(h.state, h.callbacks, "", "a"));

    EXPECT_TRUE(paste_properties(h.state, h.callbacks, "", "b"));

    const atp::runtime::module_node& b = h.module_at("b");
    ASSERT_EQ(b.properties.size(), 2u);
    EXPECT_EQ(b.properties.at(0).first, "step");
    EXPECT_EQ(b.properties.at(0).second, 5);
    EXPECT_EQ(b.properties.at(1).first, "loud");
    EXPECT_EQ(b.properties.at(1).second, true);
}

TEST(UiPropertyActions, PasteIsOneUndoStepHoweverManyValuesItCarries) {
    harness h;
    h.state.doc.add_module("", "gain", "a");
    h.state.doc.set_property("", "a", "step", 5);
    h.state.doc.set_property("", "a", "loud", true);
    h.state.doc.add_module("", "gain", "b");
    ASSERT_TRUE(copy_properties(h.state, h.callbacks, "", "a"));
    ASSERT_TRUE(paste_properties(h.state, h.callbacks, "", "b"));

    ASSERT_TRUE(h.state.doc.undo());

    EXPECT_TRUE(h.module_at("b").properties.empty());
}

TEST(UiPropertyActions, AValueTheTargetDoesNotDeclareIsSkippedAndNamed) {
    harness h;
    h.state.doc.add_module("", "gain", "a");
    h.state.doc.set_property("", "a", "step", 5);
    h.state.doc.set_property("", "a", "loud", true);
    h.state.doc.add_module("", "plain", "b");
    ASSERT_TRUE(copy_properties(h.state, h.callbacks, "", "a"));
    h.log.clear();

    EXPECT_TRUE(paste_properties(h.state, h.callbacks, "", "b"));

    const atp::runtime::module_node& b = h.module_at("b");
    ASSERT_EQ(b.properties.size(), 1u);
    EXPECT_EQ(b.properties.front().first, "step");
    ASSERT_EQ(h.log.size(), 1u);
    EXPECT_NE(h.log.front().find("skipped loud"), std::string::npos);
}

TEST(UiPropertyActions, AValueOutsideTheTargetsSetIsSkippedRatherThanWritten) {
    harness h;
    h.state.doc.add_module("", "wide", "a");
    h.state.doc.set_property("", "a", "channels", 6);
    h.state.doc.add_module("", "narrow", "b");
    ASSERT_TRUE(copy_properties(h.state, h.callbacks, "", "a"));
    h.log.clear();

    EXPECT_FALSE(paste_properties(h.state, h.callbacks, "", "b"));

    EXPECT_TRUE(h.module_at("b").properties.empty());
    ASSERT_EQ(h.log.size(), 1u);
    EXPECT_NE(h.log.front().find("takes none of"), std::string::npos);
    EXPECT_NE(h.log.front().find("channels"), std::string::npos);
}

TEST(UiPropertyActions, AValueOfAnotherKindIsSkipped) {
    harness h;
    h.state.doc.add_module("", "wide", "a");
    h.state.doc.set_property("", "a", "label", "deep");
    h.state.doc.add_module("", "narrow", "b");
    ASSERT_TRUE(copy_properties(h.state, h.callbacks, "", "a"));

    EXPECT_FALSE(paste_properties(h.state, h.callbacks, "", "b"));

    EXPECT_TRUE(h.module_at("b").properties.empty());
}

TEST(UiPropertyActions, APropertyTheTargetKeepsOutOfTheProjectIsSkipped) {
    harness h;
    h.state.doc.add_module("", "wide", "a");
    h.state.doc.set_property("", "a", "step", 5);
    h.state.doc.add_module("", "narrow", "b");
    ASSERT_TRUE(copy_properties(h.state, h.callbacks, "", "a"));

    EXPECT_FALSE(paste_properties(h.state, h.callbacks, "", "b"));

    EXPECT_TRUE(h.module_at("b").properties.empty());
}

TEST(UiPropertyActions, WhatFitsIsPastedEvenWhenTheRestDoesNot) {
    harness h;
    h.state.doc.add_module("", "wide", "a");
    h.state.doc.set_property("", "a", "channels", 2);
    h.state.doc.set_property("", "a", "label", "deep");
    h.state.doc.add_module("", "narrow", "b");
    ASSERT_TRUE(copy_properties(h.state, h.callbacks, "", "a"));
    h.log.clear();

    EXPECT_TRUE(paste_properties(h.state, h.callbacks, "", "b"));

    const atp::runtime::module_node& b = h.module_at("b");
    ASSERT_EQ(b.properties.size(), 1u);
    EXPECT_EQ(b.properties.front().first, "channels");
    EXPECT_EQ(b.properties.front().second, 2);
    ASSERT_EQ(h.log.size(), 1u);
    EXPECT_NE(h.log.front().find("skipped label"), std::string::npos);
}

TEST(UiPropertyActions, PasteLeavesAValueTheClipboardSaysNothingAbout) {
    harness h;
    h.state.doc.add_module("", "gain", "a");
    h.state.doc.set_property("", "a", "step", 5);
    h.state.doc.add_module("", "gain", "b");
    h.state.doc.set_property("", "b", "loud", true);
    ASSERT_TRUE(copy_properties(h.state, h.callbacks, "", "a"));

    EXPECT_TRUE(paste_properties(h.state, h.callbacks, "", "b"));

    const atp::runtime::module_node& b = h.module_at("b");
    ASSERT_EQ(b.properties.size(), 2u);
    EXPECT_EQ(b.properties.at(0).first, "loud");
    EXPECT_EQ(b.properties.at(0).second, true);
}

TEST(UiPropertyActions, CopyingAModuleWithNothingSetSaysSo) {
    harness h;
    h.state.doc.add_module("", "gain", "a");

    EXPECT_FALSE(copy_properties(h.state, h.callbacks, "", "a"));

    EXPECT_TRUE(h.state.clip_properties.empty());
    ASSERT_EQ(h.log.size(), 1u);
    EXPECT_NE(h.log.front().find("no property set"), std::string::npos);
}

TEST(UiPropertyActions, PastingOntoAModuleWithNoFactoryIsRefusedAndReported) {
    harness h;
    h.state.doc.add_module("", "gain", "a");
    h.state.doc.set_property("", "a", "step", 5);
    h.state.doc.add_module("", "ghost", "b");
    ASSERT_TRUE(copy_properties(h.state, h.callbacks, "", "a"));
    h.log.clear();

    EXPECT_FALSE(paste_properties(h.state, h.callbacks, "", "b"));

    EXPECT_TRUE(h.module_at("b").properties.empty());
    ASSERT_EQ(h.log.size(), 1u);
    EXPECT_NE(h.log.front().find("no factory"), std::string::npos);
}

TEST(UiPropertyActions, PastingAnEmptyClipboardIsNotAnOperation) {
    harness h;
    h.state.doc.add_module("", "gain", "a");

    EXPECT_FALSE(paste_properties(h.state, h.callbacks, "", "a"));

    EXPECT_TRUE(h.log.empty());
}

TEST(UiPropertyActions, CopyDoesNotTouchTheNodeClipboard) {
    harness h;
    h.state.doc.add_module("", "gain", "a");
    h.state.doc.set_property("", "a", "step", 5);
    h.state.clip = h.state.doc.copy_children("", {"a"});

    EXPECT_TRUE(copy_properties(h.state, h.callbacks, "", "a"));

    EXPECT_EQ(h.state.clip.nodes.size(), 1u);
}

}  // namespace
