#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <QString>

#include "model/clipboard_actions.hpp"

namespace {

using atp::studio::ui::app_state;
using atp::studio::ui::copy_nodes;
using atp::studio::ui::cut_nodes;
using atp::studio::ui::paste_nodes;
using atp::studio::ui::ui_callbacks;

struct harness {
    app_state state;
    ui_callbacks callbacks;
    std::vector<std::string> log;
    int refreshes = 0;

    harness() {
        callbacks.project_changed = [this] { ++refreshes; };
        callbacks.error = [this](const QString& text) { log.push_back(text.toStdString()); };
        callbacks.selection_changed = [] {};
    }
};

TEST(UiClipboardActions, CopyFillsTheClipboardAndReportsIt) {
    harness h;
    h.state.doc.add_module("", "src", "a");

    EXPECT_TRUE(copy_nodes(h.state, h.callbacks, "", {"a"}));

    EXPECT_EQ(h.state.clip.nodes.size(), 1u);
    ASSERT_EQ(h.log.size(), 1u);
    EXPECT_NE(h.log.front().find("copied"), std::string::npos);
    EXPECT_EQ(h.refreshes, 0);
}

TEST(UiClipboardActions, CopyOfNothingDoesNotTouchTheClipboard) {
    harness h;
    h.state.doc.add_module("", "src", "a");
    (void)copy_nodes(h.state, h.callbacks, "", {"a"});
    h.log.clear();

    EXPECT_FALSE(copy_nodes(h.state, h.callbacks, "", {}));

    EXPECT_EQ(h.state.clip.nodes.size(), 1u);
    EXPECT_TRUE(h.log.empty());
}

TEST(UiClipboardActions, CopyReportsABadNameAndLeavesTheClipboardAlone) {
    harness h;
    h.state.doc.add_module("", "src", "a");

    EXPECT_FALSE(copy_nodes(h.state, h.callbacks, "", {"ghost"}));

    EXPECT_TRUE(h.state.clip.empty());
    ASSERT_EQ(h.log.size(), 1u);
    EXPECT_NE(h.log.front().find("copy:"), std::string::npos);
}

TEST(UiClipboardActions, CutFillsTheClipboardAndRemovesTheNode) {
    harness h;
    h.state.doc.add_module("", "src", "a");
    h.state.selected_child = "a";

    EXPECT_TRUE(cut_nodes(h.state, h.callbacks, "", {"a"}));

    EXPECT_EQ(h.state.clip.nodes.size(), 1u);
    EXPECT_TRUE(h.state.doc.group_at("")->modules.empty());
    EXPECT_TRUE(h.state.selected_child.empty());
}

TEST(UiClipboardActions, NoGestureRebuildsTheWidgetsOnItsOwn) {
    harness h;
    h.state.doc.add_module("", "src", "a");

    EXPECT_TRUE(copy_nodes(h.state, h.callbacks, "", {"a"}));
    EXPECT_TRUE(cut_nodes(h.state, h.callbacks, "", {"a"}));
    EXPECT_FALSE(paste_nodes(h.state, h.callbacks, "", std::nullopt).empty());

    EXPECT_EQ(h.refreshes, 0);
}

TEST(UiClipboardActions, AFailedCutIsReportedAsACut) {
    harness h;
    h.state.doc.add_module("", "src", "a");

    EXPECT_FALSE(cut_nodes(h.state, h.callbacks, "", {"ghost"}));

    ASSERT_EQ(h.log.size(), 1u);
    EXPECT_NE(h.log.front().find("cut:"), std::string::npos);
    EXPECT_EQ(h.log.front().find("copy:"), std::string::npos);
}

TEST(UiClipboardActions, PasteInsertsAndReturnsTheNewNames) {
    harness h;
    h.state.doc.add_module("", "src", "a");
    (void)copy_nodes(h.state, h.callbacks, "", {"a"});

    const std::vector<std::string> made = paste_nodes(h.state, h.callbacks, "", std::nullopt);

    EXPECT_EQ(made, std::vector<std::string>{"a_2"});
    EXPECT_EQ(h.state.doc.group_at("")->modules.size(), 2u);
}

TEST(UiClipboardActions, PasteOfAnEmptyClipboardIsNotAnOperation) {
    harness h;
    h.state.doc.add_module("", "src", "a");

    EXPECT_TRUE(paste_nodes(h.state, h.callbacks, "", std::nullopt).empty());

    EXPECT_EQ(h.state.doc.group_at("")->modules.size(), 1u);
    EXPECT_TRUE(h.log.empty());
}

TEST(UiClipboardActions, PasteReportsAnUnknownGroup) {
    harness h;
    h.state.doc.add_module("", "src", "a");
    (void)copy_nodes(h.state, h.callbacks, "", {"a"});
    h.log.clear();

    EXPECT_TRUE(paste_nodes(h.state, h.callbacks, "nowhere", std::nullopt).empty());

    ASSERT_EQ(h.log.size(), 1u);
    EXPECT_NE(h.log.front().find("paste:"), std::string::npos);
}

}  // namespace
