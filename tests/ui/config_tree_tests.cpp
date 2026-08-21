// SPDX-License-Identifier: Apache-2.0
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QStyledItemDelegate>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>

#include <atp/config.hpp>
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

struct grid_config : atp::config::fields {
    using fields::fields;
    std::int64_t& size = field("size", std::int64_t{16});
    double& gain = field("gain", 1.0);
    bool& invert = field("invert", false);
    std::string& device = field<std::string>("device");
};

class grid_module : public atp::module<atp::ports<>, "grid_demo"> {
   public:
    using config_type = grid_config;
    explicit grid_module(const atp::module_config& cfg) : config_(cfg) {}

   private:
    grid_config config_;
};

class plain_module : public atp::module<atp::ports<>, "plain_demo"> {};

struct grid_channel : atp::config::fields {
    using fields::fields;
    std::int64_t& index = field("index", std::int64_t{0});
};

struct grid_list_config : atp::config::fields {
    using fields::fields;
    std::deque<grid_channel>& channels = list<grid_channel>("channels");
    std::deque<std::string>& tags = list<std::string>("tags");
};

class grid_list_module : public atp::module<atp::ports<>, "grid_list"> {
   public:
    using config_type = grid_list_config;
    explicit grid_list_module(const atp::module_config& cfg) : config_(cfg) {}

   private:
    grid_list_config config_;
};

struct grid_reals_config : atp::config::fields {
    using fields::fields;
    std::deque<double>& gains = list<double>("gains");
    std::int64_t& channel = field<std::int64_t>("channel");
};

class grid_reals_module : public atp::module<atp::ports<>, "grid_reals"> {
   public:
    using config_type = grid_reals_config;
    explicit grid_reals_module(const atp::module_config& cfg) : config_(cfg) {}

   private:
    grid_reals_config config_;
};

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
    pretend_running running;

    explicit harness(const char* factory = "grid_demo") {
        callbacks.project_changed = [] {};
        callbacks.error = [this](const QString& text) { last_error = text; };
        callbacks.selection_changed = [] {};
        state.manager.registry().add<grid_module>();
        state.manager.registry().add<plain_module>();
        state.manager.registry().add<grid_list_module>();
        state.manager.registry().add<grid_reals_module>();
        state.doc.add_module("", factory, "a");
        state.current_group = "";
        state.selected_child = "a";
    }

    void set_config(const atp::config::node& value) {
        state.doc.set_config("", "a", value);
    }

    [[nodiscard]] atp::config::node module_config() {
        const atp::config::node* found = state.doc.config_of("", "a");
        return found == nullptr ? atp::config::node() : *found;
    }

    void start_pretend_running() {
        state.view = &running;
    }
};

[[nodiscard]] QTreeWidgetItem* node_at(const inspector_widget& inspector, const QString& path) {
    QTreeWidget* tree = inspector.findChild<QTreeWidget*>("config_tree");
    if (tree == nullptr) {
        return nullptr;
    }
    for (QTreeWidgetItemIterator it(tree); *it != nullptr; ++it) {
        if ((*it)->data(0, Qt::UserRole + 1).toString() == path && (*it)->childCount() == 0) {
            return *it;
        }
    }
    return nullptr;
}

[[nodiscard]] QString value_of(const inspector_widget& inspector, const QString& path) {
    QTreeWidgetItem* item = node_at(inspector, path);
    return item == nullptr ? QString() : item->text(1);
}

void type_into(const inspector_widget& inspector, const QString& path, const QString& text) {
    QTreeWidgetItem* item = node_at(inspector, path);
    if (item != nullptr) {
        item->setText(1, text);
    }
}

}  // namespace

TEST(UiConfigTree, ADeclaredConfigIsShownAsRowsAndNotAsJson) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.set_config(atp::config::node::object({{"device", "hw:0"}}));

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    EXPECT_EQ(inspector.findChild<QPlainTextEdit*>(), nullptr)
        << "a module that declared its config is edited by rows, not by text";
    EXPECT_NE(node_at(inspector, "size"), nullptr);
    EXPECT_NE(node_at(inspector, "invert"), nullptr);
}

TEST(UiConfigTree, AModuleWithoutASchemaKeepsTheJsonEditor) {
    (void)atp_ui_tests::ensure_app();
    harness h("plain_demo");
    h.set_config(atp::config::node::object({{"anything", 1}}));

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    EXPECT_NE(inspector.findChild<QPlainTextEdit*>(), nullptr)
        << "there are no fields to draw, so nothing about the old path may change";
}

TEST(UiConfigTree, EachKindGetsItsOwnEditor) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.set_config(atp::config::node::object({{"device", "hw:0"}}));

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    EXPECT_NE(inspector.findChild<QCheckBox*>("config_edit_invert"), nullptr);
    EXPECT_NE(node_at(inspector, "size"), nullptr);
    EXPECT_NE(node_at(inspector, "gain"), nullptr);
}

TEST(UiConfigTree, AnAbsentFieldShowsItsDefaultAndIsNotWritten) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.set_config(atp::config::node::object({{"device", "hw:0"}}));

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    QTreeWidgetItem* size = node_at(inspector, "size");
    ASSERT_NE(size, nullptr);
    EXPECT_EQ(size->text(1), QStringLiteral("16"));
    EXPECT_EQ(h.module_config().find("size"), nullptr)
        << "showing a default is not writing one: the document must not grow by opening a module";
}

TEST(UiConfigTree, EditingAFieldWritesOnlyThatField) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.set_config(atp::config::node::object({{"device", "hw:0"}}));

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    QTreeWidgetItem* size = node_at(inspector, "size");
    ASSERT_NE(size, nullptr);
    size->setText(1, QStringLiteral("32"));

    EXPECT_EQ(h.module_config().at("size"), 32);
    EXPECT_EQ(h.module_config().at("device"), "hw:0");
    EXPECT_EQ(h.module_config().find("gain"), nullptr) << "an untouched default stays out of the document";
}

TEST(UiConfigTree, AValueBackAtItsDefaultLeavesTheDocument) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.set_config(atp::config::node::object({{"device", "hw:0"}, {"size", 32}}));

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    QTreeWidgetItem* size = node_at(inspector, "size");
    ASSERT_NE(size, nullptr);
    size->setText(1, QStringLiteral("16"));

    EXPECT_EQ(h.module_config().find("size"), nullptr);
}

TEST(UiConfigTree, AnUnparsableValueRollsBackAndSaysWhyOnTheRow) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.set_config(atp::config::node::object({{"device", "hw:0"}, {"size", 32}}));

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    QTreeWidgetItem* size = node_at(inspector, "size");
    ASSERT_NE(size, nullptr);
    size->setText(1, QStringLiteral("big"));

    EXPECT_EQ(h.module_config().at("size"), 32) << "a refused value must not reach the document";
    EXPECT_EQ(size->text(1), QStringLiteral("32")) << "and the row must go back to what is actually stored";
    EXPECT_FALSE(inspector.findChild<QTreeWidget*>("config_tree")->toolTip().isEmpty())
        << "the reason belongs on the row, not in the log across the window";
}

TEST(UiConfigTree, AnEmptiedRequiredFieldBecomesAbsentRatherThanEmpty) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.set_config(atp::config::node::object({{"device", "hw:0"}}));

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    QTreeWidgetItem* device = node_at(inspector, "device");
    ASSERT_NE(device, nullptr);
    device->setText(1, QString{});

    EXPECT_EQ(h.module_config().find("device"), nullptr)
        << "an emptied required field is absence, not an empty value: the module then fails with "
           "'required and absent' instead of being handed an empty string";
}

TEST(UiConfigTree, AWholeNumberIsAcceptedInARealField) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.set_config(atp::config::node::object({{"device", "hw:0"}}));

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    QTreeWidgetItem* gain = node_at(inspector, "gain");
    ASSERT_NE(gain, nullptr);
    gain->setText(1, QStringLiteral("3"));

    EXPECT_TRUE(h.module_config().at("gain").is_double())
        << "the SDK widens a whole number into a real; the form must not be stricter than what will read it";
}

TEST(UiConfigTree, ABooleanIsWrittenByItsCheckBox) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.set_config(atp::config::node::object({{"device", "hw:0"}}));

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    QCheckBox* invert = inspector.findChild<QCheckBox*>("config_edit_invert");
    ASSERT_NE(invert, nullptr);
    invert->setChecked(true);

    EXPECT_EQ(h.module_config().at("invert"), true);
}

TEST(UiConfigTree, TheRowsAreReadOnlyWhileThePipelineRuns) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.set_config(atp::config::node::object({{"device", "hw:0"}}));
    h.start_pretend_running();

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    QTreeWidget* tree = inspector.findChild<QTreeWidget*>("config_tree");
    ASSERT_NE(tree, nullptr);
    EXPECT_FALSE(tree->isEnabled()) << "structure is read-only while running, and a config is structure";
}

TEST(UiConfigTree, AListOfScalarsGetsOneRowPerItemAndAnAddButton) {
    (void)atp_ui_tests::ensure_app();
    harness h("grid_list");
    h.set_config(atp::config::node::object({{"tags", atp::config::node::array({"live", "stage"})}}));

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    EXPECT_NE(node_at(inspector, "tags[0]"), nullptr);
    EXPECT_NE(node_at(inspector, "tags[1]"), nullptr);
    EXPECT_EQ(node_at(inspector, "tags[2]"), nullptr);
    EXPECT_NE(inspector.findChild<QAbstractButton*>("config_add_tags"), nullptr);
}

TEST(UiConfigTree, AddingAnItemWritesItAndRebuildsTheRows) {
    (void)atp_ui_tests::ensure_app();
    harness h("grid_list");
    h.set_config(atp::config::node::object({{"tags", atp::config::node::array({"live"})}}));

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    QAbstractButton* add = inspector.findChild<QAbstractButton*>("config_add_tags");
    ASSERT_NE(add, nullptr);
    add->click();

    ASSERT_EQ(h.module_config().at("tags").size(), 2u);
    EXPECT_EQ(h.module_config().at("tags")[1], atp::config::node("")) << "a new scalar item starts empty, not absent";
    EXPECT_NE(node_at(inspector, "tags[1]"), nullptr);
}

TEST(UiConfigTree, RemovingAnItemClosesTheGapRatherThanLeavingAHole) {
    (void)atp_ui_tests::ensure_app();
    harness h("grid_list");
    h.set_config(atp::config::node::object({{"tags", atp::config::node::array({"live", "stage", "rehearsal"})}}));

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    QAbstractButton* remove = inspector.findChild<QAbstractButton*>("config_remove_tags_1");
    ASSERT_NE(remove, nullptr);
    remove->click();

    ASSERT_EQ(h.module_config().at("tags").size(), 2u);
    EXPECT_EQ(h.module_config().at("tags")[0], atp::config::node("live"));
    EXPECT_EQ(h.module_config().at("tags")[1], atp::config::node("rehearsal"));
}

TEST(UiConfigTree, EditingAnItemOfAScalarListWritesItInPlace) {
    (void)atp_ui_tests::ensure_app();
    harness h("grid_list");
    h.set_config(atp::config::node::object({{"tags", atp::config::node::array({"live", "stage"})}}));

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    QTreeWidgetItem* second = node_at(inspector, "tags[1]");
    ASSERT_NE(second, nullptr);
    second->setText(1, QStringLiteral("studio"));

    ASSERT_EQ(h.module_config().at("tags").size(), 2u);
    EXPECT_EQ(h.module_config().at("tags")[0], atp::config::node("live"));
    EXPECT_EQ(h.module_config().at("tags")[1], atp::config::node("studio"));
}

TEST(UiConfigTree, AListOfGroupsShowsEachElementsFields) {
    (void)atp_ui_tests::ensure_app();
    harness h("grid_list");
    h.set_config(atp::config::node::object(
        {{"channels", atp::config::node::array({atp::config::node::object({{"index", 7}})})}}));

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    QTreeWidgetItem* index = node_at(inspector, "channels[0].index");
    ASSERT_NE(index, nullptr);
    EXPECT_EQ(index->text(1), QStringLiteral("7"));
}

TEST(UiConfigTree, AnEmptyListIsNotWrittenAtAll) {
    (void)atp_ui_tests::ensure_app();
    harness h("grid_list");
    h.set_config(atp::config::node(atp::config::node::object_type{}));

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    EXPECT_NE(inspector.findChild<QAbstractButton*>("config_add_tags"), nullptr);
    EXPECT_EQ(h.module_config().find("tags"), nullptr)
        << "a list nobody filled is the default, and defaults stay out of the document";
}

TEST(UiConfigTree, AFreshlyPlacedModuleGetsTheRowsAndNotTheJsonEditor) {
    (void)atp_ui_tests::ensure_app();
    harness h;

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    EXPECT_EQ(inspector.findChild<QPlainTextEdit*>(), nullptr)
        << "a module dropped on the canvas has no config yet, and that is exactly when a form is most "
           "useful — an absent config is the empty case of the schema, not a foreign format";
    QTreeWidgetItem* size = node_at(inspector, "size");
    ASSERT_NE(size, nullptr);
    EXPECT_EQ(size->text(1), QStringLiteral("16"));
    EXPECT_TRUE(h.module_config().is_null()) << "and drawing the defaults still writes nothing";
}

TEST(UiConfigTree, AnElementWhoseFieldsAreAllDefaultsStaysInTheList) {
    (void)atp_ui_tests::ensure_app();
    harness h("grid_list");
    h.set_config(atp::config::node::object(
        {{"channels", atp::config::node::array({atp::config::node::object({{"index", 7}})})}}));

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    QTreeWidgetItem* index = node_at(inspector, "channels[0].index");
    ASSERT_NE(index, nullptr);
    index->setText(1, QStringLiteral("0"));

    ASSERT_NE(h.module_config().find("channels"), nullptr)
        << "an element at its defaults is still an element; dropping it deletes the whole list";
    ASSERT_EQ(h.module_config().at("channels").size(), 1u);
    EXPECT_EQ(h.module_config().at("channels")[0].size(), 0u)
        << "it says nothing because every field sits at its default, and it comes back as those defaults";
}

TEST(UiConfigTree, ADefaultElementDoesNotPunchAHoleInTheArray) {
    (void)atp_ui_tests::ensure_app();
    harness h("grid_list");
    h.set_config(atp::config::node::object(
        {{"channels", atp::config::node::array(
                          {atp::config::node::object({{"index", 0}}), atp::config::node::object({{"index", 9}})})}}));

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    QTreeWidgetItem* second = node_at(inspector, "channels[1].index");
    ASSERT_NE(second, nullptr);
    second->setText(1, QStringLiteral("11"));

    const atp::config::node channels = h.module_config().at("channels");
    ASSERT_EQ(channels.size(), 2u);
    EXPECT_TRUE(channels[0].is_object()) << "a null here is what makes the pipeline refuse to build";
    EXPECT_EQ(channels[0].size(), 0u) << "all its fields are defaults, so it is written as an empty element";
    EXPECT_EQ(channels[1].at("index"), atp::config::node(11));
}

TEST(UiConfigTree, AKeyTheSchemaDoesNotDeclareSurvivesAnEdit) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.set_config(atp::config::node::object({{"device", "hw:0"}, {"legacy", true}}));

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    QTreeWidgetItem* size = node_at(inspector, "size");
    ASSERT_NE(size, nullptr);
    size->setText(1, QStringLiteral("32"));

    EXPECT_NE(h.module_config().find("legacy"), nullptr)
        << "a config written by hand or by a newer plugin must not lose keys the form cannot show";
    EXPECT_EQ(h.module_config().at("size"), 32);
}

TEST(UiConfigTree, AnUntouchedRequiredFieldIsNotWrittenAsEmpty) {
    (void)atp_ui_tests::ensure_app();
    harness h;

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    QCheckBox* invert = inspector.findChild<QCheckBox*>("config_edit_invert");
    ASSERT_NE(invert, nullptr);
    invert->setChecked(true);

    EXPECT_EQ(h.module_config().at("invert"), true);
    EXPECT_EQ(h.module_config().find("device"), nullptr)
        << "writing \"\" would satisfy 'required and absent' and hand the module an empty value instead";
}

TEST(UiConfigTree, ARealKeepsItsDigitsInsteadOfBeingRounded) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.set_config(atp::config::node::object({{"device", "hw:0"}, {"gain", 44100.125}}));

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    QTreeWidgetItem* gain = node_at(inspector, "gain");
    ASSERT_NE(gain, nullptr);
    EXPECT_EQ(gain->text(1), QStringLiteral("44100.125")) << "six significant digits would show 44100.1";

    QTreeWidgetItem* size = node_at(inspector, "size");
    ASSERT_NE(size, nullptr);
    size->setText(1, QStringLiteral("32"));

    EXPECT_DOUBLE_EQ(h.module_config().at("gain").as_double(), 44100.125)
        << "an unrelated edit must not rewrite a value it merely re-read";
}

TEST(UiConfigTree, WritingAFieldItsOwnValueChangesNothingAtAll) {
    (void)atp_ui_tests::ensure_app();
    harness h;

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    QTreeWidgetItem* size = node_at(inspector, "size");
    ASSERT_NE(size, nullptr);
    size->setText(1, size->text(1));

    EXPECT_TRUE(h.module_config().is_null())
        << "writing a field the value it already had is not an edit, and must leave neither a config nor an undo step";
}

TEST(UiConfigTree, TheRowsFollowTheDocumentWhenItChangesBehindTheForm) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.set_config(atp::config::node::object({{"device", "hw:0"}, {"size", 32}}));

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    h.set_config(atp::config::node::object({{"device", "hw:0"}, {"size", 64}}));
    inspector.refresh();

    QTreeWidgetItem* size = node_at(inspector, "size");
    ASSERT_NE(size, nullptr);
    EXPECT_EQ(size->text(1), QStringLiteral("64"))
        << "stale rows would write the pre-undo value back on the next keystroke";
}

TEST(UiConfigTree, AModuleWithASchemaStillOffersTheSourceRowAndSharing) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.set_config(atp::config::node::object({{"device", "hw:0"}}));

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    EXPECT_NE(inspector.findChild<QComboBox*>("config_source"), nullptr)
        << "without it there is no way back to a shared block or a file from the GUI";
    EXPECT_NE(inspector.findChild<QLineEdit*>("config_share_name"), nullptr);
}

TEST(UiConfigTree, AConfigThatNamesASharedBlockKeepsTheJsonEditor) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.state.doc.set_shared_config("rig", atp::config::node::object({{"device", "hw:9"}}));
    h.set_config(atp::config::node("rig"));

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    EXPECT_NE(inspector.findChild<QPlainTextEdit*>(), nullptr)
        << "a block belongs to the document and may be named by modules whose schemas differ";
    EXPECT_EQ(node_at(inspector, "size"), nullptr);
}

TEST(UiConfigTree, AKeyTheSchemaDoesNotDeclareIsShownAndNotHidden) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.set_config(atp::config::node::object({{"device", "hw:0"}, {"legacy", true}}));

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    EXPECT_NE(node_at(inspector, "legacy"), nullptr)
        << "the form could only preserve it; a tree of the object can show it";
}

TEST(UiConfigTree, EveryDeclaredFieldIsThereBeforeAnybodyWroteOne) {
    (void)atp_ui_tests::ensure_app();
    harness h;

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    EXPECT_NE(node_at(inspector, "size"), nullptr);
    EXPECT_NE(node_at(inspector, "gain"), nullptr);
    EXPECT_NE(node_at(inspector, "device"), nullptr) << "including a required one, shown empty";
    EXPECT_TRUE(h.module_config().is_null()) << "shown is not written";
}

TEST(UiConfigTree, AFieldNameCannotBeEdited) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.set_config(atp::config::node::object({{"device", "hw:0"}}));

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    QTreeWidget* tree = inspector.findChild<QTreeWidget*>("config_tree");
    ASSERT_NE(tree, nullptr);
    QTreeWidgetItem* size = node_at(inspector, "size");
    ASSERT_NE(size, nullptr);

    const QModelIndex name_cell = tree->indexFromItem(size, 0);
    const QModelIndex value_cell = tree->indexFromItem(size, 1);
    QStyleOptionViewItem option;

    EXPECT_EQ(tree->itemDelegate()->createEditor(tree, option, name_cell), nullptr)
        << "a field name is what the module declared, not something the document may rename";
    QWidget* editor = tree->itemDelegate()->createEditor(tree, option, value_cell);
    EXPECT_NE(editor, nullptr) << "and the value beside it is still editable";
    delete editor;
}

TEST(UiConfigTree, NestingIsIndentedGentlyEnoughToReadTheNames) {
    (void)atp_ui_tests::ensure_app();
    harness h("grid_list");
    h.set_config(atp::config::node::object(
        {{"channels", atp::config::node::array({atp::config::node::object({{"index", 7}})})}}));

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    QTreeWidget* tree = inspector.findChild<QTreeWidget*>("config_tree");
    ASSERT_NE(tree, nullptr);
    EXPECT_LE(tree->indentation(), 12)
        << "a config nests three levels deep, and the default indent pushes the names out of the column";
}

TEST(UiConfigTree, AKeyWithADotOrABracketIsStillEditable) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.set_config(atp::config::node::object({{"device", "hw:0"}, {"a.b", "dotted"}, {"c[d", "bracketed"}}));

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    QTreeWidgetItem* dotted = node_at(inspector, "a.b");
    ASSERT_NE(dotted, nullptr);
    dotted->setText(1, QStringLiteral("edited"));
    EXPECT_EQ(h.module_config().at("a.b"), "edited")
        << "a path parsed back out of text reads this as key 'a', finds nothing and drops the edit";

    QTreeWidgetItem* bracketed = node_at(inspector, "c[d");
    ASSERT_NE(bracketed, nullptr);
    bracketed->setText(1, QStringLiteral("also edited"));
    EXPECT_EQ(h.module_config().at("c[d"), "also edited") << "and this one used to throw out of a Qt slot";
}

TEST(UiConfigTree, AFractionCanBeTypedIntoAnElementOfARealList) {
    (void)atp_ui_tests::ensure_app();
    harness h("grid_reals");
    h.set_config(atp::config::node::object({{"gains", atp::config::node::array({2})}}));

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    QTreeWidgetItem* first = node_at(inspector, "gains[0]");
    ASSERT_NE(first, nullptr);
    first->setText(1, QStringLiteral("2.5"));

    EXPECT_DOUBLE_EQ(h.module_config().at("gains")[0].as_double(), 2.5)
        << "the element's kind comes from the declaration, not from guessing at the value on screen";
}

TEST(UiConfigTree, ABooleanRefusesTextItCannotRead) {
    (void)atp_ui_tests::ensure_app();
    harness h;
    h.set_config(atp::config::node::object({{"device", "hw:0"}, {"flag", true}}));

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    QTreeWidgetItem* flag = node_at(inspector, "flag");
    ASSERT_NE(flag, nullptr);
    flag->setText(1, QStringLiteral("yes"));

    EXPECT_EQ(h.module_config().at("flag"), true) << "'yes' silently becoming false is worse than a refusal";
    EXPECT_EQ(flag->text(1), QStringLiteral("true"));
}

TEST(UiConfigTree, ARequiredIntegerMayBeZero) {
    (void)atp_ui_tests::ensure_app();
    harness h("grid_reals");

    inspector_widget inspector(h.state, h.callbacks);
    inspector.refresh();

    QTreeWidgetItem* channel = node_at(inspector, "channel");
    ASSERT_NE(channel, nullptr);
    EXPECT_TRUE(channel->text(1).isEmpty()) << "an unset required field is an empty cell";
    channel->setText(1, QStringLiteral("0"));

    ASSERT_NE(h.module_config().find("channel"), nullptr)
        << "0 is a value somebody typed, and dropping it makes the module fail on 'required and absent'";
    EXPECT_EQ(h.module_config().at("channel"), 0);
}
