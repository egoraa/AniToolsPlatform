// SPDX-License-Identifier: Apache-2.0
#include <string>

#include <gtest/gtest.h>

#include <QIcon>
#include <QImage>
#include <QPixmap>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>

#include "model/app_state.hpp"
#include "panels/project_tree.hpp"
#include "ui/qt_app.hpp"

namespace {

using atp::studio::ui::app_state;
using atp::studio::ui::project_tree;
using atp::studio::ui::ui_callbacks;

ui_callbacks quiet_callbacks() {
    ui_callbacks callbacks;
    callbacks.project_changed = [] {};
    callbacks.error = [](const QString&) {};
    callbacks.selection_changed = [] {};
    return callbacks;
}

atp::studio::module_info described_as(const std::string& factory, const std::string& source) {
    atp::studio::module_info info;
    info.name = factory;
    info.source = source;
    return info;
}

QImage row_mark(project_tree& tree, const QString& name) {
    for (QTreeWidgetItemIterator it(&tree); *it != nullptr; ++it) {
        if ((*it)->text(0) == name) {
            return (*it)->icon(0).pixmap(32, 32).toImage();
        }
    }
    return {};
}

TEST(UiProjectTree, AScriptModuleWearsTheMarkOfItsLanguageInTheTreeToo) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks = quiet_callbacks();

    state.doc.add_module("", "atp.demo.source", "plain");
    state.doc.add_module("", "my_filter", "scripted");
    state.describe_cache.emplace("atp.demo.source@latest", described_as("atp.demo.source", ""));
    state.describe_cache.emplace("my_filter@latest", described_as("my_filter", "/modules/my_filter.py"));

    project_tree tree(state, callbacks);
    tree.refresh();

    const QImage plain = row_mark(tree, QStringLiteral("plain"));
    const QImage scripted = row_mark(tree, QStringLiteral("scripted"));
    ASSERT_FALSE(plain.isNull());
    ASSERT_FALSE(scripted.isNull());
    EXPECT_NE(plain, scripted);
}

}  // namespace
