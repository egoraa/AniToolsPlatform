// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_UI_EDITOR_HPP
#define ATP_STUDIO_UI_EDITOR_HPP

#include <filesystem>

#include <QDesktopServices>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QUrl>

namespace atp::studio::ui {

/// Splits an editor command and puts the file into it: at `{file}` where the command says so,
/// appended otherwise. The substitution happens after the split, so a path with spaces stays one
/// argument.
/// @param command the command from the settings, never empty
/// @param file path to open
/// @return the program followed by its arguments
[[nodiscard]] inline QStringList editor_arguments(const QString& command, const QString& file) {
    QStringList parts = QProcess::splitCommand(command);
    bool placed = false;
    for (QString& part : parts) {
        if (part.contains(QStringLiteral("{file}"))) {
            part.replace(QStringLiteral("{file}"), file);
            placed = true;
        }
    }
    if (!placed) {
        parts << file;
    }
    return parts;
}

/// Opens a file for editing: the configured command, or the desktop's own association when none is
/// set. The association is the honest default, and also the reason the setting exists — a `.py` is
/// commonly associated with the interpreter, which would run the script instead of showing it.
/// @param command the command from the settings, possibly empty
/// @param file path to open
/// @return false if nothing could be started, which is worth saying in the Log
[[nodiscard]] inline bool open_in_editor(const QString& command, const std::filesystem::path& file) {
    const QString path = QString::fromStdWString(file.wstring());
    if (command.trimmed().isEmpty()) {
        return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
    QStringList parts = editor_arguments(command, path);
    if (parts.isEmpty()) {
        return false;
    }
    const QString program = parts.takeFirst();
    return QProcess::startDetached(program, parts);
}

}  // namespace atp::studio::ui

#endif
