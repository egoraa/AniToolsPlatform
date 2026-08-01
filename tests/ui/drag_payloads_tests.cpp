#include <memory>

#include <gtest/gtest.h>

#include <QMimeData>

#include "model/drag_payloads.hpp"

namespace {

using atp::studio::ui::decode_module_mime;
using atp::studio::ui::decode_node_mime;
using atp::studio::ui::encode_group_mime;
using atp::studio::ui::encode_module_mime;
using atp::studio::ui::encode_node_mime;
using atp::studio::ui::is_group_mime;
using atp::studio::ui::module_mime_payload;
using atp::studio::ui::node_mime_payload;

TEST(UiDragPayloads, ModuleSurvivesTheRoundTrip) {
    const std::unique_ptr<QMimeData> mime(encode_module_mime({"demo", "1.2.0", "C:/plugins/demo.dll"}));
    module_mime_payload out;
    ASSERT_TRUE(decode_module_mime(mime.get(), out));
    EXPECT_EQ(out.factory.toStdString(), "demo");
    EXPECT_EQ(out.version.toStdString(), "1.2.0");
    EXPECT_EQ(out.plugin.toStdString(), "C:/plugins/demo.dll");
}

TEST(UiDragPayloads, ModuleDecodeRejectsForeignData) {
    QMimeData text;
    text.setText("dragged from a text editor");
    module_mime_payload out;
    EXPECT_FALSE(decode_module_mime(&text, out));
    EXPECT_FALSE(decode_module_mime(nullptr, out));
}

TEST(UiDragPayloads, ModuleDecodeRejectsANamelessFactory) {
    const std::unique_ptr<QMimeData> mime(encode_module_mime({"", "1.0.0", "demo.dll"}));
    module_mime_payload out;
    EXPECT_FALSE(decode_module_mime(mime.get(), out));
}

TEST(UiDragPayloads, GroupIsItsOwnFormat) {
    const std::unique_ptr<QMimeData> mime(encode_group_mime());
    EXPECT_TRUE(is_group_mime(mime.get()));
    module_mime_payload as_module;
    EXPECT_FALSE(decode_module_mime(mime.get(), as_module));
    node_mime_payload as_node;
    EXPECT_FALSE(decode_node_mime(mime.get(), as_node));
}

TEST(UiDragPayloads, NodeSurvivesTheRoundTripFromTheRoot) {
    const std::unique_ptr<QMimeData> mime(encode_node_mime({"", "stage"}));
    node_mime_payload out;
    ASSERT_TRUE(decode_node_mime(mime.get(), out));
    EXPECT_EQ(out.group_path.toStdString(), "");
    EXPECT_EQ(out.name.toStdString(), "stage");
}

TEST(UiDragPayloads, NodeSurvivesTheRoundTripFromANestedGroup) {
    const std::unique_ptr<QMimeData> mime(encode_node_mime({"a.b", "gain"}));
    node_mime_payload out;
    ASSERT_TRUE(decode_node_mime(mime.get(), out));
    EXPECT_EQ(out.group_path.toStdString(), "a.b");
    EXPECT_EQ(out.name.toStdString(), "gain");
}

TEST(UiDragPayloads, NodeDecodeRejectsANamelessNode) {
    const std::unique_ptr<QMimeData> mime(encode_node_mime({"a", ""}));
    node_mime_payload out;
    EXPECT_FALSE(decode_node_mime(mime.get(), out));
}

TEST(UiDragPayloads, MoveIsNotAnAddition) {
    const std::unique_ptr<QMimeData> mime(encode_node_mime({"a", "b"}));
    module_mime_payload as_module;
    EXPECT_FALSE(decode_module_mime(mime.get(), as_module));
    EXPECT_FALSE(is_group_mime(mime.get()));
}

}  // namespace
