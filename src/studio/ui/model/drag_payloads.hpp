// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_UI_DRAG_PAYLOADS_HPP
#define ATP_STUDIO_UI_DRAG_PAYLOADS_HPP

#include <QByteArray>
#include <QDataStream>
#include <QIODevice>
#include <QMimeData>
#include <QString>

/// What the editor's drags carry, one MIME format per kind of gesture: a module and a group dragged
/// out of the palette, and a node moved inside the project tree. The formats live here rather than
/// with either end of a drag, so that a view accepting a drop need not include the view that starts
/// it — and so that a new drop target has one place to look for what it may receive.
namespace atp::studio::ui {

/// A MIME type of our own, so only this application's canvas accepts the drag and foreign drops
/// (files, text) leave the scene alone.
inline constexpr const char* module_mime_type = "application/x-atp-module";

/// What a dragged palette item carries.
struct module_mime_payload {
    QString factory;
    QString version;
    QString plugin;
};

/// Packs a payload into MIME data owned by the caller.
[[nodiscard]] inline QMimeData* encode_module_mime(const module_mime_payload& payload) {
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream << payload.factory << payload.version << payload.plugin;
    auto* mime = new QMimeData;
    mime->setData(module_mime_type, bytes);
    return mime;
}

/// Unpacks a payload from MIME data.
/// @return false if the data is missing, of another format or malformed
[[nodiscard]] inline bool decode_module_mime(const QMimeData* mime, module_mime_payload& out) {
    if (mime == nullptr || !mime->hasFormat(module_mime_type)) {
        return false;
    }
    QByteArray bytes = mime->data(module_mime_type);
    QDataStream stream(&bytes, QIODevice::ReadOnly);
    stream >> out.factory >> out.version >> out.plugin;
    return stream.status() == QDataStream::Ok && !out.factory.isEmpty();
}

/// A group is dragged out of the palette under a format of its own. It carries no factory, and
/// decode_module_mime already reads an empty factory as corrupt data, so "this is a group" must not
/// be smuggled through the module format.
inline constexpr const char* group_mime_type = "application/x-atp-group";

/// Packs group MIME data owned by the caller. The payload is a constant: there is nothing to say
/// beyond the format itself, and an empty QByteArray is a needless edge case for the receiver.
[[nodiscard]] inline QMimeData* encode_group_mime() {
    auto* mime = new QMimeData;
    mime->setData(group_mime_type, QByteArray("group"));
    return mime;
}

/// Whether the MIME data is a group dragged out of the palette.
[[nodiscard]] inline bool is_group_mime(const QMimeData* mime) {
    return mime != nullptr && mime->hasFormat(group_mime_type);
}

/// A node dragged inside the project tree. A move is not an addition, so it travels under a format
/// of its own: the canvas accepts the palette's formats and must not mistake a move for one.
inline constexpr const char* node_mime_type = "application/x-atp-node";

/// Which node is being moved: the group that currently holds it ("" is the root) and its name.
struct node_mime_payload {
    QString group_path;
    QString name;
};

/// Packs a moved node into MIME data owned by the caller.
[[nodiscard]] inline QMimeData* encode_node_mime(const node_mime_payload& payload) {
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream << payload.group_path << payload.name;
    auto* mime = new QMimeData;
    mime->setData(node_mime_type, bytes);
    return mime;
}

/// Unpacks a moved node from MIME data.
/// @return false if the data is missing, of another format or malformed
[[nodiscard]] inline bool decode_node_mime(const QMimeData* mime, node_mime_payload& out) {
    if (mime == nullptr || !mime->hasFormat(node_mime_type)) {
        return false;
    }
    QByteArray bytes = mime->data(node_mime_type);
    QDataStream stream(&bytes, QIODevice::ReadOnly);
    stream >> out.group_path >> out.name;
    return stream.status() == QDataStream::Ok && !out.name.isEmpty();
}

}  // namespace atp::studio::ui

#endif
