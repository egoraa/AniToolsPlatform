// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_UI_NEW_SCRIPT_MODULE_HPP
#define ATP_STUDIO_UI_NEW_SCRIPT_MODULE_HPP

#include "model/app_state.hpp"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <optional>
#include <ranges>
#include <string>

#include <QString>
#include <QStringList>

#include <atp/studio/languages.hpp>
#include <atp/studio/script_modules.hpp>

namespace atp::studio::ui {

/// Writes the profile, reporting a failure instead of raising it.
///
/// A profile that cannot be written must not undo a module that already exists on disk, so this is a
/// warning and not an error: the gesture succeeded, only its memory of the directory did not.
inline void save_settings_quietly(app_state& state, ui_callbacks& callbacks) {
    try {
        studio::save_settings(state.settings, state.settings_file);
    } catch (const std::exception& e) {
        callbacks.report(QString::fromStdString(std::string("new module: ") + e.what()), atp::log_level::warning);
    }
}

/// Creates a module of one language in a folder that carries everything needed to host it, and makes
/// the palette show it.
///
/// The folder is given the shape an installation has — the language's bridge in it, its package and
/// the scripts in the language's own subdirectory — so it is portable, a host pointed at it needs
/// nothing else, and an editor opened on the script resolves the import with no project configuration.
/// Whether that copied bridge is then *loaded* depends on the session and on the language: a bridge
/// already loaded is reloaded instead, and for a language that serves one bridge per process the
/// extras are dropped afterwards.
///
/// The project is deliberately left alone: creating a module and using one are separate gestures, and
/// they meet in the palette. Every step after the file is written can fail without taking the file
/// with it — a missing source bridge or an unwritable profile is reported and the path is still
/// returned, so the author never loses what they just wrote.
/// @param state the session
/// @param callbacks where the lines go
/// @param lang the language being authored in
/// @param folder the folder the person chose; the script lands in the language's subdirectory of it
/// @param name the platform name of the module
/// @param source a bridge to copy into the folder when it holds none
/// @return the file written, or nullopt when nothing was written
[[nodiscard]] inline std::optional<std::filesystem::path> create_script_module_action(
    app_state& state,
    ui_callbacks& callbacks,
    const studio::script_language& lang,
    const std::filesystem::path& folder,
    const std::string& name,
    const studio::bridge_source& source) {
    studio::folder_setup setup;
    try {
        setup = studio::provision_folder(folder, source, lang);
    } catch (const std::exception& e) {
        callbacks.report(QString::fromStdString(std::string("new ") + std::string(lang.label) + " module: " + e.what()),
                         atp::log_level::error);
        return std::nullopt;
    }
    if (setup.bridge_copied || setup.package_copied) {
        QStringList copied;
        if (setup.bridge_copied) {
            copied << QString::fromStdString(studio::bridge_filename(lang));
        }
        if (setup.package_copied) {
            copied << QStringLiteral("the atp package");
        }
        callbacks.report(QString("copied %1 into %2")
                             .arg(copied.join(QStringLiteral(" and ")), QString::fromStdWString(folder.wstring())),
                         atp::log_level::info);
    }
    if (setup.package_refreshed) {
        callbacks.report(QString("refreshed the atp package in %1: the copy there was older than the platform's")
                             .arg(QString::fromStdWString(setup.scripts_dir.wstring())),
                         atp::log_level::info);
    }
    if (setup.bridge_stale) {
        callbacks.report(
            QString("the %1 in %2 is older than %3 and is left as it is; delete it if a module of this "
                    "folder misbehaves")
                .arg(QString::fromStdString(studio::bridge_filename(lang)), QString::fromStdWString(folder.wstring()),
                     QString::fromStdWString(source.bridge.wstring())),
            atp::log_level::warning);
    }
    if (!setup.bridge_copied && !setup.package_copied && !source.found()) {
        QStringList looked;
        for (const std::filesystem::path& place : source.searched) {
            looked << QString::fromStdWString(place.wstring());
        }
        callbacks.report(QString("no %1 to copy into %2; looked in %3")
                             .arg(QString::fromStdString(studio::bridge_filename(lang)),
                                  QString::fromStdWString(folder.wstring()), looked.join(QStringLiteral(", "))),
                         atp::log_level::warning);
    }

    std::filesystem::path file;
    try {
        file = studio::create_script_module(setup.scripts_dir, name, lang);
    } catch (const std::exception& e) {
        callbacks.report(QString::fromStdString(std::string("new ") + std::string(lang.label) + " module: " + e.what()),
                         atp::log_level::error);
        return std::nullopt;
    }
    callbacks.report(QString("wrote %1").arg(QString::fromStdWString(file.wstring())), atp::log_level::info);

    if (std::ranges::find(state.manager.search_dirs(), folder) == state.manager.search_dirs().end()) {
        state.manager.add_search_dir(folder);
        state.settings.search_dirs.push_back(folder.string());
        callbacks.report(QString("%1 is a module search directory now").arg(QString::fromStdWString(folder.wstring())),
                         atp::log_level::info);
    }
    state.script_env.apply(state.manager.search_dirs());
    save_settings_quietly(state, callbacks);

    if (state.view->running()) {
        callbacks.report(QString("'%1' will reach the palette once the pipeline is stopped: loading or reloading a "
                                 "bridge would unregister factories the running tree is holding")
                             .arg(QString::fromStdString(name)),
                         atp::log_level::warning);
        callbacks.project_changed();
        return file;
    }

    if (const std::optional<std::filesystem::path> loaded = studio::find_bridge(state.manager, lang)) {
        (void)state.manager.reload_plugin(*loaded);
    } else {
        state.manager.rescan();
    }
    for (const std::filesystem::path& dropped : studio::keep_one_bridge(state.manager, lang)) {
        callbacks.report(QString::fromStdString(studio::dropped_bridge_note(dropped, lang)), atp::log_level::info);
    }
    state.invalidate_descriptions();
    callbacks.project_changed();
    if (state.manager.registry().find(name) == nullptr) {
        const studio::plugin_info* row = studio::bridge_row(state.manager, lang);
        if (row == nullptr) {
            callbacks.report(
                QString("'%1' is not in the palette: no %2 is among the plugins at all")
                    .arg(QString::fromStdString(name), QString::fromStdString(studio::bridge_filename(lang))),
                atp::log_level::warning);
        } else if (!row->loaded) {
            QString said = QString("'%1' is not in the palette because the bridge at %2 did not load: %3")
                               .arg(QString::fromStdString(name), QString::fromStdWString(row->path.wstring()),
                                    QString::fromStdString(row->error));
            if (studio::reads_as_missing_dependency(row->error) && !lang.missing_dependency_hint.empty()) {
                said += QString::fromUtf8(lang.missing_dependency_hint.data(),
                                          static_cast<qsizetype>(lang.missing_dependency_hint.size()));
            }
            callbacks.report(said, atp::log_level::error);
        } else {
            callbacks.report(QString("'%1' is not in the palette although the bridge at %2 loaded and offers %3 "
                                     "module(s); the script was skipped and the bridge said why on its own error "
                                     "stream")
                                 .arg(QString::fromStdString(name), QString::fromStdWString(row->path.wstring()))
                                 .arg(row->modules.size()),
                             atp::log_level::warning);
        }
    } else {
        callbacks.report(QString("'%1' is in the palette").arg(QString::fromStdString(name)), atp::log_level::info);
    }
    return file;
}

}  // namespace atp::studio::ui

#endif
