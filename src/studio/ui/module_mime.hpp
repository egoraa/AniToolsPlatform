#ifndef ATP_STUDIO_UI_MODULE_MIME_HPP
#define ATP_STUDIO_UI_MODULE_MIME_HPP

#include <QByteArray>
#include <QDataStream>
#include <QIODevice>
#include <QMimeData>
#include <QString>

namespace atp::studio::ui {

// Свой MIME-тип: перетаскивание принимает только канвас этого приложения,
// чужие drop'ы (файлы, текст) сцену не трогают. Формат общий для палитры и
// сцены и живёт отдельно, чтобы сцене не включать заголовок палитры.
inline constexpr const char* module_mime_type = "application/x-atp-module";

struct module_mime_payload {
    QString factory;
    QString version;
    QString plugin;
};

[[nodiscard]] inline QMimeData* encode_module_mime(const module_mime_payload& payload) {
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream << payload.factory << payload.version << payload.plugin;
    auto* mime = new QMimeData;
    mime->setData(module_mime_type, bytes);
    return mime;
}

[[nodiscard]] inline bool decode_module_mime(const QMimeData* mime, module_mime_payload& out) {
    if (mime == nullptr || !mime->hasFormat(module_mime_type)) {
        return false;
    }
    QByteArray bytes = mime->data(module_mime_type);
    QDataStream stream(&bytes, QIODevice::ReadOnly);
    stream >> out.factory >> out.version >> out.plugin;
    return stream.status() == QDataStream::Ok && !out.factory.isEmpty();
}

}  // namespace atp::studio::ui

#endif  // ATP_STUDIO_UI_MODULE_MIME_HPP
