#ifndef ATP_STUDIO_UI_MODULE_MIME_HPP
#define ATP_STUDIO_UI_MODULE_MIME_HPP

#include <QByteArray>
#include <QDataStream>
#include <QIODevice>
#include <QMimeData>
#include <QString>

namespace atp::studio::ui {

/// A MIME type of our own, so only this application's canvas accepts the drag and foreign drops
/// (files, text) leave the scene alone. The format is shared by the palette and the scene and lives
/// apart so that the scene need not include the palette's header.
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

}  // namespace atp::studio::ui

#endif  // ATP_STUDIO_UI_MODULE_MIME_HPP
