// SPDX-License-Identifier: Apache-2.0
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QFocusEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QString>

#include <atp/config/node.hpp>
#include <atp/module.hpp>
#include <atp/runtime/json_codec.hpp>

#include "model/app_state.hpp"
#include "panels/inspector_widget.hpp"
#include "ui/qt_app.hpp"

namespace {

using atp::studio::ui::app_state;
using atp::studio::ui::inspector_widget;
using atp::studio::ui::ui_callbacks;

struct knob_props : atp::io::properties {
    atp::io::property<int>& step = make<atp::io::property<int>>("step", 1);
};
class knob_module : public atp::module<atp::ports<atp::io::inputs, atp::io::outputs, knob_props>, "knob"> {};

class pretend_running final : public atp::studio::runtime_view_base {
   public:
    [[nodiscard]] bool running() const override {
        return true;
    }
    [[nodiscard]] std::string error_text() const override {
        return {};
    }
    [[nodiscard]] std::vector<atp::runtime::pipeline_runner::thread_stats> stats() const override {
        return {};
    }
    [[nodiscard]] std::vector<atp::runtime::connection_sample> sample_connections() const override {
        return {};
    }
    [[nodiscard]] std::vector<atp::runtime::group::module_stats> module_metrics() const override {
        return {};
    }
    [[nodiscard]] std::vector<atp::runtime::group::port_stats> input_metrics() const override {
        return {};
    }
    [[nodiscard]] bool metrics_enabled() const override {
        return false;
    }
    bool set_metrics_enabled(bool) override {
        return false;
    }
    [[nodiscard]] std::vector<atp::studio::live_property> live_properties(const std::string&) const override {
        return {};
    }
    void set_property(const atp::runtime::property_override&) override {}
};

struct harness {
    app_state state;
    ui_callbacks callbacks;
    QString last_error;

    harness() {
        callbacks.project_changed = [] {};
        callbacks.error = [this](const QString& text) { last_error = text; };
        callbacks.selection_changed = [] {};
        state.manager.registry().add<knob_module>();
        state.doc.add_module("", "knob", "a");
        state.current_group = "";
        state.selected_child = "a";
    }

    [[nodiscard]] QPlainTextEdit* editor(inspector_widget& inspector) const {
        return inspector.findChild<QPlainTextEdit*>();
    }

    [[nodiscard]] static QComboBox* source(inspector_widget& inspector) {
        return inspector.findChild<QComboBox*>("config_source");
    }

    [[nodiscard]] static QLineEdit* share_name(inspector_widget& inspector) {
        return inspector.findChild<QLineEdit*>("config_share_name");
    }

    [[nodiscard]] static QPushButton* share_button(inspector_widget& inspector) {
        return inspector.findChild<QPushButton*>("config_share");
    }

    static void lose_focus(QPlainTextEdit* editor) {
        QFocusEvent event(QEvent::FocusOut, Qt::MouseFocusReason);
        QApplication::sendEvent(editor, &event);
    }
};

}  // namespace

TEST(UiInspectorConfig, ShowsTheConfigAsJson) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.state.doc.set_config("", "a", atp::runtime::json_parse(R"({"channels": [1, 2]})"));

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    QPlainTextEdit* editor = h.editor(inspector);
    ASSERT_NE(editor, nullptr);
    EXPECT_EQ(atp::runtime::json_parse(editor->toPlainText().toStdString()),
              atp::runtime::json_parse(R"({"channels": [1, 2]})"));
}

TEST(UiInspectorConfig, AnInlineConfigNamesInlineAsItsSource) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.state.doc.set_config("", "a", atp::runtime::json_parse(R"({"channels": [1]})"));

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    ASSERT_NE(harness::source(inspector), nullptr);
    EXPECT_EQ(harness::source(inspector)->currentText(), QStringLiteral("(inline)"));
}

TEST(UiInspectorConfig, AReferenceShowsTheBlockItNamesRatherThanTheName) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.state.doc.set_shared_config("rig", atp::runtime::json_parse(R"({"channels": [6]})"));
    h.state.doc.set_config("", "a", atp::config::node("rig"));

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    EXPECT_EQ(harness::source(inspector)->currentText(), QStringLiteral("rig"))
        << "the reference belongs in the source row, which is where it can be changed";
    EXPECT_EQ(atp::runtime::json_parse(h.editor(inspector)->toPlainText().toStdString()),
              atp::runtime::json_parse(R"({"channels": [6]})"))
        << "the one editor shows the config that actually reaches the module";
}

TEST(UiInspectorConfig, ChoosingASharedBlockPointsTheModuleAtIt) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.state.doc.set_shared_config("rig", atp::runtime::json_parse(R"({"channels": [6]})"));
    h.state.doc.set_config("", "a", atp::runtime::json_parse(R"({"channels": [1]})"));
    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    harness::source(inspector)->setCurrentText(QStringLiteral("rig"));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    ASSERT_NE(h.state.doc.config_of("", "a"), nullptr);
    EXPECT_EQ(*h.state.doc.config_of("", "a"), atp::config::node("rig"));
    EXPECT_EQ(atp::runtime::json_parse(h.editor(inspector)->toPlainText().toStdString()),
              atp::runtime::json_parse(R"({"channels": [6]})"))
        << "the one editor follows the source row and now shows the block";
}

TEST(UiInspectorConfig, GoingBackToInlineKeepsTheContentAndLeavesTheBlockDeclared) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.state.doc.set_shared_config("rig", atp::runtime::json_parse(R"({"channels": [6]})"));
    h.state.doc.set_config("", "a", atp::config::node("rig"));
    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    harness::source(inspector)->setCurrentText(QStringLiteral("(inline)"));

    ASSERT_NE(h.state.doc.config_of("", "a"), nullptr);
    EXPECT_EQ(*h.state.doc.config_of("", "a"), atp::runtime::json_parse(R"({"channels": [6]})"))
        << "detaching must copy what the module was getting, not silently empty it";
    ASSERT_NE(h.state.doc.shared_config("rig"), nullptr) << "the block belongs to the document, not to this module";
}

TEST(UiInspectorConfig, EditingAReferencedConfigWritesIntoTheSharedBlock) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.state.doc.set_shared_config("rig", atp::runtime::json_parse(R"({"channels": [6]})"));
    h.state.doc.set_config("", "a", atp::config::node("rig"));
    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    QPlainTextEdit* editor = h.editor(inspector);
    editor->setPlainText(QStringLiteral(R"({"channels": [1, 2]})"));
    harness::lose_focus(editor);

    ASSERT_NE(h.state.doc.shared_config("rig"), nullptr);
    EXPECT_EQ(*h.state.doc.shared_config("rig"), atp::runtime::json_parse(R"({"channels": [1, 2]})"));
    EXPECT_EQ(*h.state.doc.config_of("", "a"), atp::config::node("rig"))
        << "editing through a reference must never expand it into the module's node";
}

TEST(UiInspectorConfig, SharingAnInlineConfigDeclaresTheBlockAndPointsAtIt) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.state.doc.set_config("", "a", atp::runtime::json_parse(R"({"channels": [1]})"));
    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    ASSERT_NE(harness::share_name(inspector), nullptr);
    harness::share_name(inspector)->setText(QStringLiteral("rig"));
    harness::share_button(inspector)->click();

    ASSERT_NE(h.state.doc.shared_config("rig"), nullptr);
    EXPECT_EQ(*h.state.doc.shared_config("rig"), atp::runtime::json_parse(R"({"channels": [1]})"));
    ASSERT_NE(h.state.doc.config_of("", "a"), nullptr);
    EXPECT_EQ(*h.state.doc.config_of("", "a"), atp::config::node("rig"));
    EXPECT_TRUE(h.last_error.isEmpty());
}

TEST(UiInspectorConfig, SharingUnderANameAlreadyDeclaredIsRefused) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.state.doc.set_shared_config("rig", atp::runtime::json_parse(R"({"channels": [6]})"));
    h.state.doc.set_config("", "a", atp::runtime::json_parse(R"({"channels": [1]})"));
    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    harness::share_name(inspector)->setText(QStringLiteral("rig"));
    harness::share_button(inspector)->click();

    EXPECT_FALSE(h.last_error.isEmpty());
    EXPECT_EQ(*h.state.doc.shared_config("rig"), atp::runtime::json_parse(R"({"channels": [6]})"))
        << "sharing must not overwrite a block other modules may be using; the source row is how one joins it";
    EXPECT_EQ(*h.state.doc.config_of("", "a"), atp::runtime::json_parse(R"({"channels": [1]})"));
}

TEST(UiInspectorConfig, ARefusedShareLeavesTheEditorWorking) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.state.doc.set_config("", "a", atp::runtime::json_parse(R"({"channels": [1]})"));
    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    QPlainTextEdit* editor = h.editor(inspector);
    ASSERT_NE(editor, nullptr);
    editor->setPlainText(QStringLiteral("[1, 2]"));
    harness::share_name(inspector)->setText(QStringLiteral("rig"));
    harness::share_button(inspector)->click();

    EXPECT_FALSE(h.last_error.isEmpty());
    EXPECT_EQ(h.state.doc.shared_config("rig"), nullptr);

    h.last_error = QString();
    editor->setPlainText(QStringLiteral(R"({"channels": [6]})"));
    harness::lose_focus(editor);

    ASSERT_NE(h.state.doc.config_of("", "a"), nullptr);
    EXPECT_EQ(*h.state.doc.config_of("", "a"), atp::runtime::json_parse(R"({"channels": [6]})"))
        << "a refused share leaves the editor on screen, so it must stay connected: no rebuild follows a "
           "refusal, and every later edit would be dropped without a word";
}

TEST(UiInspectorConfig, AReferencedConfigOffersNoSharingRow) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.state.doc.set_shared_config("rig", atp::runtime::json_parse(R"({"channels": [6]})"));
    h.state.doc.set_config("", "a", atp::config::node("rig"));

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    EXPECT_EQ(harness::share_button(inspector), nullptr) << "it is already shared; the source row says under what name";
}

TEST(UiInspectorConfig, EditingWritesAnObjectIntoTheProject) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    QPlainTextEdit* editor = h.editor(inspector);
    ASSERT_NE(editor, nullptr);
    editor->setPlainText(QStringLiteral(R"({"channels": [1, 2, 6]})"));
    harness::lose_focus(editor);

    ASSERT_NE(h.state.doc.config_of("", "a"), nullptr);
    EXPECT_EQ(*h.state.doc.config_of("", "a"), atp::runtime::json_parse(R"({"channels": [1, 2, 6]})"));
    EXPECT_TRUE(h.last_error.isEmpty());
}

TEST(UiInspectorConfig, MalformedTextIsReportedAndNeverReachesTheProject) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    QPlainTextEdit* editor = h.editor(inspector);
    editor->setPlainText(QStringLiteral("{\"channels\": [1, 2"));
    harness::lose_focus(editor);

    EXPECT_EQ(h.state.doc.config_of("", "a"), nullptr);
    EXPECT_FALSE(h.last_error.isEmpty());

    ASSERT_TRUE(h.state.doc.undo());
    EXPECT_TRUE(h.state.doc.config().pipeline.modules.empty())
        << "one undo has to reach past the refused edit to adding the module, since a half-typed object "
           "must push no snapshot of its own";
}

TEST(UiInspectorConfig, ClearingTheTextDropsTheConfig) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.state.doc.set_config("", "a", atp::runtime::json_parse(R"({"channels": [1]})"));
    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    QPlainTextEdit* editor = h.editor(inspector);
    editor->setPlainText(QString());
    harness::lose_focus(editor);

    EXPECT_EQ(h.state.doc.config_of("", "a"), nullptr);
}

TEST(UiInspectorConfig, TheEditorOutlivesTheDeletionOfTheOneItReplaced) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.state.doc.add_module("", "knob", "b");

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();
    h.state.selected_child = "b";
    inspector.refresh();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    QPlainTextEdit* editor = h.editor(inspector);
    ASSERT_NE(editor, nullptr);
    editor->setPlainText(QStringLiteral(R"({"channels": [6]})"));
    harness::lose_focus(editor);

    ASSERT_NE(h.state.doc.config_of("", "b"), nullptr)
        << "the deferred delete of the previous editor must not clear the pointer to this one";
    EXPECT_EQ(*h.state.doc.config_of("", "b"), atp::runtime::json_parse(R"({"channels": [6]})"));
}

TEST(UiInspectorConfig, ACommitReachesTheGroupTheEditorWasBuiltFor) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.state.doc.add_group("", "g");
    h.state.doc.add_module("g", "knob", "a");

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();
    QPlainTextEdit* editor = h.editor(inspector);
    ASSERT_NE(editor, nullptr);
    editor->setPlainText(QStringLiteral(R"({"channels": [1]})"));

    h.state.current_group = "g";
    h.state.selected_child = "a";
    harness::lose_focus(editor);

    ASSERT_NE(h.state.doc.config_of("", "a"), nullptr)
        << "the selection moved before the focus left, so only the group captured at build time is right";
    EXPECT_EQ(*h.state.doc.config_of("", "a"), atp::runtime::json_parse(R"({"channels": [1]})"));
    EXPECT_EQ(h.state.doc.config_of("g", "a"), nullptr) << "the same name in another group must not be written";
}

TEST(UiInspectorConfig, AChangeBehindTheEditorReachesItWithoutARebuild) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.state.doc.set_config("", "a", atp::runtime::json_parse(R"({"channels": [1]})"));
    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();
    QPlainTextEdit* editor = h.editor(inspector);
    ASSERT_NE(editor, nullptr);

    ASSERT_TRUE(h.state.doc.undo());
    inspector.refresh();

    EXPECT_TRUE(editor->toPlainText().isEmpty())
        << "the selection did not change, so refresh syncs rather than rebuilds";
}

TEST(UiInspectorConfig, AnUndoneConfigIsNotWrittenBackByTheNextFocusOut) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.state.doc.set_config("", "a", atp::runtime::json_parse(R"({"channels": [1]})"));
    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();
    QPlainTextEdit* editor = h.editor(inspector);
    ASSERT_NE(editor, nullptr);

    ASSERT_TRUE(h.state.doc.undo());
    inspector.refresh();
    harness::lose_focus(editor);

    EXPECT_EQ(h.state.doc.config_of("", "a"), nullptr);
}

TEST(UiInspectorConfig, ARunningPipelineLocksTheConfigButNotTheProperties) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();
    QPlainTextEdit* editor = h.editor(inspector);
    ASSERT_NE(editor, nullptr);
    ASSERT_TRUE(editor->isEnabled());

    pretend_running live;
    h.state.view = &live;
    inspector.refresh();

    EXPECT_FALSE(editor->isEnabled()) << "a config reaches a constructor, so it belongs to the structure";

    QLineEdit* step = nullptr;
    for (QLineEdit* candidate : inspector.findChildren<QLineEdit*>()) {
        if (candidate->text() == QStringLiteral("1")) {
            step = candidate;
        }
    }
    ASSERT_NE(step, nullptr) << "the row of the int property 'step' is a line edit holding its value";
    EXPECT_TRUE(step->isEnabled()) << "a property stays live while the pipeline runs";
}

TEST(UiInspectorConfig, AFileConfigShowsTheFileReadOnlyAndOffersNoSharing) {
    (void)atp_ui_tests::ensure_app();
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "atp_inspector_file_config";
    std::error_code ignored;
    std::filesystem::remove_all(dir, ignored);
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "rig.ini", std::ios::binary) << "rate = 48000\n";

    harness h;
    h.state.doc_path = dir / "p.atp.json";
    h.state.doc.set_config("", "a", atp::config::node("file:rig.ini"));

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    QPlainTextEdit* editor = h.editor(inspector);
    ASSERT_NE(editor, nullptr);
    EXPECT_EQ(editor->toPlainText(), QStringLiteral("rate = 48000\n"));
    EXPECT_TRUE(editor->isReadOnly()) << "the file belongs to whoever wrote it, not to studio";
    EXPECT_EQ(harness::share_button(inspector), nullptr) << "there is no object here to declare as a block";

    QComboBox* source = harness::source(inspector);
    ASSERT_NE(source, nullptr);
    EXPECT_EQ(source->currentText(), QStringLiteral("file:rig.ini"));

    std::filesystem::remove_all(dir, ignored);
}

TEST(UiInspectorConfig, ASharedBlockNamingAFileIsAsReadOnlyAsTheFileItself) {
    (void)atp_ui_tests::ensure_app();
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "atp_inspector_shared_file_config";
    std::error_code ignored;
    std::filesystem::remove_all(dir, ignored);
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "rig.ini", std::ios::binary) << "rate = 48000\n";

    harness h;
    h.state.doc_path = dir / "p.atp.json";
    h.state.doc.set_shared_config("printing", atp::config::node("file:rig.ini"));
    h.state.doc.set_config("", "a", atp::config::node("printing"));

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    QPlainTextEdit* editor = h.editor(inspector);
    ASSERT_NE(editor, nullptr);
    EXPECT_EQ(editor->toPlainText(), QStringLiteral("rate = 48000\n"));
    EXPECT_TRUE(editor->isReadOnly()) << "the reference is followed before deciding: a file is a file however reached";

    QComboBox* source = harness::source(inspector);
    ASSERT_NE(source, nullptr);
    EXPECT_EQ(source->currentText(), QStringLiteral("printing")) << "the row names where this module points";

    editor->setPlainText(QStringLiteral(R"({"channels": [1]})"));
    harness::lose_focus(editor);

    ASSERT_NE(h.state.doc.shared_config("printing"), nullptr);
    EXPECT_EQ(*h.state.doc.shared_config("printing"), atp::config::node("file:rig.ini"))
        << "committing an object here would drop the file reference for every module naming the block";

    std::filesystem::remove_all(dir, ignored);
}

TEST(UiInspectorConfig, ClearingAReferencedConfigDetachesTheModuleAndKeepsTheBlock) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.state.doc.set_shared_config("printing", atp::runtime::json_parse(R"({"channels": [6]})"));
    h.state.doc.set_config("", "a", atp::config::node("printing"));
    h.state.doc.add_module("", "knob", "b");
    h.state.doc.set_config("", "b", atp::config::node("printing"));

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    QPlainTextEdit* editor = h.editor(inspector);
    ASSERT_NE(editor, nullptr);
    editor->setPlainText(QString());
    harness::lose_focus(editor);

    EXPECT_EQ(h.state.doc.config_of("", "a"), nullptr) << "the module that was cleared keeps no config";
    ASSERT_NE(h.state.doc.shared_config("printing"), nullptr)
        << "a block belongs to the document; clearing one module's editor must not delete it";
    EXPECT_EQ(*h.state.doc.shared_config("printing"), atp::runtime::json_parse(R"({"channels": [6]})"));
    ASSERT_NE(h.state.doc.config_of("", "b"), nullptr) << "the other module naming it is left pointing at a block";
    EXPECT_EQ(*h.state.doc.config_of("", "b"), atp::config::node("printing"));
}

TEST(UiInspectorConfig, AFileThatCannotBeReadShowsTheReasonInsteadOfNothing) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.state.doc.set_config("", "a", atp::config::node("file:rig.ini"));

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    QPlainTextEdit* editor = h.editor(inspector);
    ASSERT_NE(editor, nullptr);
    EXPECT_TRUE(editor->toPlainText().contains(QStringLiteral("needs the document's directory")))
        << editor->toPlainText().toStdString();
}

TEST(UiInspectorConfig, AFileConfigIsNeverWrittenBackByAFocusOut) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.state.doc.set_config("", "a", atp::config::node("file:rig.ini"));

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();
    QPlainTextEdit* editor = h.editor(inspector);
    ASSERT_NE(editor, nullptr);
    harness::lose_focus(editor);

    ASSERT_NE(h.state.doc.config_of("", "a"), nullptr);
    EXPECT_EQ(*h.state.doc.config_of("", "a"), "file:rig.ini")
        << "the preview is not a config, and committing it would replace the reference with its text";
}
