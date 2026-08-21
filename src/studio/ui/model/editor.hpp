// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_UI_EDITOR_HPP
#define ATP_STUDIO_UI_EDITOR_HPP

#include <filesystem>
#include <string>

#include <QDesktopServices>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QUrl>

#include "model/app_state.hpp"

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

/// Opens a file in the configured editor, saying so in the Log when nothing could be started.
///
/// The wording of that one failure lives here rather than at each gesture: File > New module, the
/// plugins dock, the canvas and the project tree all open the same kind of file and owe the same
/// answer when no editor starts. Whatever the editor itself then makes of the file is its own
/// business and not ours to report.
/// @param file path as text, already decoded — see module_source() for why that matters
inline void open_source(app_state& state, ui_callbacks& callbacks, const QString& file) {
    if (!open_in_editor(QString::fromStdString(state.settings.editor_command),
                        std::filesystem::path(file.toStdWString()))) {
        callbacks.report(QString("could not open an editor for %1; open it by hand").arg(file),
                         atp::log_level::warning);
    }
}

/// The file the module `child` of `group` was declared in — the script behind a script module.
///
/// What the gesture is offered on: a module has something to open only when its plugin named a file,
/// which a bridge does for every script it registers and a compiled plugin does for nothing. So an
/// empty answer is the ordinary case and means "no such item in the menu", not an error.
///
/// The path is decoded through QString because it arrives as UTF-8 — what plugin_c.h promises of
/// every string crossing that boundary — while std::filesystem::path's narrow constructor reads the
/// process code page on Windows and would mangle a script folder named outside it.
/// @param group group holding the module ("" is the root)
/// @param child module name within that group
/// @return the file, empty for a group, for a factory the manager does not know, and for a module
///         whose plugin named no file
[[nodiscard]] inline QString module_source(app_state& state, const std::string& group, const std::string& child) {
    const runtime::group_node* g = state.doc.group_at(group);
    if (g == nullptr) {
        return {};
    }
    for (const runtime::child_node& c : g->modules) {
        if (c.module && c.module->name == child) {
            const module_info* info = state.describe_cached(c.module->factory, c.module->factory_version);
            return info == nullptr ? QString() : QString::fromStdString(info->source);
        }
    }
    return {};
}

}  // namespace atp::studio::ui

#endif
