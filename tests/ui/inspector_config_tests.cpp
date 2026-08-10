// SPDX-License-Identifier: Apache-2.0
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <QFocusEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QString>

#include <nlohmann/json.hpp>

#include <atp/module.hpp>

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
class knob_module : public atp::module<atp::io::ports<atp::io::inputs, atp::io::outputs, knob_props>, "knob"> {};

class pretend_running final : public atp::studio::runtime_view_base {
   public:
    [[nodiscard]] bool running() const override {
        return true;
    }
    [[nodiscard]] std::string error_text() const override {
        return {};
    }
    [[nodiscard]] std::vector<atp::pipeline_runner::thread_stats> stats() const override {
        return {};
    }
    [[nodiscard]] std::vector<atp::runtime::connection_sample> sample_connections() const override {
        return {};
    }
    [[nodiscard]] std::vector<atp::group::module_stats> module_metrics() const override {
        return {};
    }
    [[nodiscard]] std::vector<atp::group::port_stats> input_metrics() const override {
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

    static void lose_focus(QPlainTextEdit* editor) {
        QFocusEvent event(QEvent::FocusOut, Qt::MouseFocusReason);
        QApplication::sendEvent(editor, &event);
    }
};

}  // namespace

TEST(UiInspectorConfig, ShowsTheConfigAsJson) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.state.doc.set_config("", "a", nlohmann::json::parse(R"({"channels": [1, 2]})"));

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    QPlainTextEdit* editor = h.editor(inspector);
    ASSERT_NE(editor, nullptr);
    EXPECT_EQ(nlohmann::json::parse(editor->toPlainText().toStdString()),
              nlohmann::json::parse(R"({"channels": [1, 2]})"));
}

TEST(UiInspectorConfig, AReferenceIsShownAsTheStringItIs) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.state.doc.set_shared_config("rig", nlohmann::json::parse(R"({"channels": [6]})"));
    h.state.doc.set_config("", "a", nlohmann::json("rig"));

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    EXPECT_EQ(h.editor(inspector)->toPlainText(), QStringLiteral("\"rig\""));
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
    EXPECT_EQ(*h.state.doc.config_of("", "a"), nlohmann::json::parse(R"({"channels": [1, 2, 6]})"));
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
    h.state.doc.set_config("", "a", nlohmann::json::parse(R"({"channels": [1]})"));
    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    QPlainTextEdit* editor = h.editor(inspector);
    editor->setPlainText(QString());
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
