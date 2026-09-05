// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_SCRIPT_MODULES_HPP
#define ATP_STUDIO_SCRIPT_MODULES_HPP

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <atp/studio/module_manager.hpp>
#include <atp/studio/script_language.hpp>

namespace atp::studio {

namespace detail {

/// Newest write time among the sources of a package, ignoring anything its runtime generates.
///
/// `__pycache__` is the reason this is not a plain recursive maximum: an interpreter writes a cache
/// the first time it imports the package, so a copy that has merely been *used* looks newer than the
/// platform's own sources and a genuinely stale package would never be refreshed. Only the language's
/// own sources count, which are also the only files the copy exists for.
/// @param dir the package directory
/// @param extension the language's source extension, dot included
[[nodiscard]] inline std::filesystem::file_time_type newest_write(const std::filesystem::path& dir,
                                                                  std::string_view extension) {
    std::error_code ec;
    std::filesystem::file_time_type newest = std::filesystem::file_time_type::min();
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != extension) {
            continue;
        }
        std::error_code when;
        const std::filesystem::file_time_type at = std::filesystem::last_write_time(entry.path(), when);
        if (!when && at > newest) {
            newest = at;
        }
    }
    return newest;
}

/// Whether a copy of the package is older than the platform's own.
///
/// A directory is compared over its sources, a single file over itself — the two languages differ in
/// what a package *is*, and this is the only place that difference costs anything.
[[nodiscard]] inline bool package_is_older(const std::filesystem::path& mine,
                                           const std::filesystem::path& theirs,
                                           const script_language& lang) {
    if (lang.package_is_directory) {
        return newest_write(mine, lang.file_extension) < newest_write(theirs, lang.file_extension);
    }
    std::error_code mine_failed;
    std::error_code theirs_failed;
    const auto mine_time = std::filesystem::last_write_time(mine, mine_failed);
    const auto theirs_time = std::filesystem::last_write_time(theirs, theirs_failed);
    return !mine_failed && !theirs_failed && theirs_time > mine_time;
}

inline void copy_package(const std::filesystem::path& from,
                         const std::filesystem::path& to,
                         const script_language& lang) {
    if (lang.package_is_directory) {
        std::filesystem::copy(
            from, to, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
        return;
    }
    std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing);
}

}  // namespace detail

/// File name of this language's bridge on this platform.
[[nodiscard]] inline std::string bridge_filename(const script_language& lang) {
    return std::string(lang.bridge_stem) + runtime::plugin_extension;
}

/// Where scripts of a module folder live: the language's subdirectory of it.
/// @param folder the folder the person chose
/// @param lang the language being authored in
/// @return the directory a script is written into
[[nodiscard]] inline std::filesystem::path scripts_dir(const std::filesystem::path& folder,
                                                       const script_language& lang) {
    return folder / lang.scripts_subdir;
}

/// Script directories derived from the plugin search directories: the language's subdirectory of each
/// one that has it.
///
/// Derived rather than remembered on purpose. A module folder is a plugin folder — that is what
/// carrying its own bridge means — so a second list of "and also look here for scripts" would show the
/// same folder twice and drift from the first list the moment either was edited by hand. The search
/// directories are the one thing a person maintains; everything a bridge is told follows from them.
/// @param search_dirs the manager's plugin search directories
/// @param lang the language whose subdirectory is looked for
/// @return existing script directories, in the order of their folders
[[nodiscard]] inline std::vector<std::string> derive_script_dirs(const std::vector<std::filesystem::path>& search_dirs,
                                                                 const script_language& lang) {
    std::vector<std::string> dirs;
    for (const std::filesystem::path& dir : search_dirs) {
        const std::filesystem::path scripts = scripts_dir(dir, lang);
        std::error_code ec;
        if (std::filesystem::is_directory(scripts, ec)) {
            dirs.push_back(scripts.string());
        }
    }
    return dirs;
}

/// The folder to offer in the new-module dialog: the last search directory shaped like a module
/// folder of this language, so the gesture continues where it left off without a setting of its own.
///
/// Asked per language rather than once, because a folder may host both: one that has `python/` and no
/// `lua/` is the right offer for Python and the wrong one for Lua.
/// @param search_dirs the manager's plugin search directories
/// @param lang the language being authored in
/// @return the folder, or nullopt when none of them holds this language's subdirectory
[[nodiscard]] inline std::optional<std::filesystem::path> last_script_folder(
    const std::vector<std::filesystem::path>& search_dirs,
    const script_language& lang) {
    for (const std::filesystem::path& dir : std::ranges::reverse_view(search_dirs)) {
        std::error_code ec;
        if (std::filesystem::is_directory(scripts_dir(dir, lang), ec)) {
            return dir;
        }
    }
    return std::nullopt;
}

/// Every loaded bridge of this language among the manager's plugins.
/// @param manager the session's module manager
/// @param lang the language whose bridge is looked for
/// @return paths of the loaded bridges, in the manager's own order
[[nodiscard]] inline std::vector<std::filesystem::path> loaded_bridges(const module_manager& manager,
                                                                       const script_language& lang) {
    std::vector<std::filesystem::path> found;
    for (const plugin_info& p : manager.plugins()) {
        if (p.loaded && p.path.stem().string() == lang.bridge_stem) {
            found.push_back(p.path);
        }
    }
    return found;
}

/// The bridge's row among the manager's plugins, loaded or failed.
///
/// The failed case is the one worth reaching: a bridge that did not load carries the reason, and
/// "the module is not in the palette" is useless next to "duplicate module 'x'" or "cannot load
/// plugin: the specified module could not be found".
/// @param manager the session's module manager
/// @param lang the language whose bridge is looked for
/// @return the row, or nullptr when no plugin file is that bridge
[[nodiscard]] inline const plugin_info* bridge_row(const module_manager& manager, const script_language& lang) {
    for (const plugin_info& p : manager.plugins()) {
        if (p.path.stem().string() == lang.bridge_stem) {
            return &p;
        }
    }
    return nullptr;
}

/// Path of the loaded bridge of this language among the manager's plugins.
/// @param manager the session's module manager
/// @param lang the language whose bridge is looked for
/// @return the path, or nullopt when no loaded plugin is that bridge
[[nodiscard]] inline std::optional<std::filesystem::path> find_bridge(const module_manager& manager,
                                                                      const script_language& lang) {
    const std::vector<std::filesystem::path> found = loaded_bridges(manager, lang);
    if (found.empty()) {
        return std::nullopt;
    }
    return found.front();
}

/// Drops every bridge file of this language but the one loaded first, and says which ones went.
///
/// This applies to **every** language, and it was briefly a per-language trait by mistake. The
/// reason it is not one: a host has one registry, and a bridge discovers its modules from the scan
/// directories derived from the one search-directory list — so two copies of the same bridge discover
/// the same scripts and register the same names. `module_registrar::add` refuses the duplicate and
/// `module_loader` withdraws the whole file, which leaves a permanent red "failed" row in the dock
/// for a file the session is deliberately not using. Nothing about that argument is CPython's; it
/// follows from one registry and one list, so it holds for any bridge that reads files.
///
/// A language may have a *stronger* reason on top — CPython's `Ctx` lives in a per-DLL static that
/// only the copy winning the inittab race fills, so its losers could not create modules even if they
/// registered — but a stronger reason for the same rule is not a different rule.
///
/// "Loaded first" is therefore the tie-break, and the extras are dropped whether they loaded or not:
/// the failed ones are the usual case rather than the exception. The one bridge that never gets
/// dropped this way is a failed one with no loaded bridge beside it: there the error is the only
/// thing the person has to go on.
/// @param manager the session's module manager
/// @param lang the language whose bridges are considered
/// @return the bridges dropped, empty when there was at most one
inline std::vector<std::filesystem::path> keep_one_bridge(module_manager& manager, const script_language& lang) {
    const std::vector<std::filesystem::path> loaded = loaded_bridges(manager, lang);
    if (loaded.empty()) {
        return {};
    }
    std::vector<std::filesystem::path> extras;
    for (const plugin_info& p : manager.plugins()) {
        if (p.path.stem().string() == lang.bridge_stem && p.path != loaded.front()) {
            extras.push_back(p.path);
        }
    }
    std::vector<std::filesystem::path> dropped;
    for (const std::filesystem::path& extra : extras) {
        if (manager.unload_plugin(extra)) {
            dropped.push_back(extra);
        }
    }
    return dropped;
}

/// What to say about a bridge keep_one_bridge dropped.
///
/// One sentence in one place because three callers need it — the dock's scan, the new-module action
/// and the startup scan — and a person meeting it in the Log has to be told the same thing each time.
/// @param dropped the bridge that was dropped
/// @param lang the language it belongs to
/// @return the line, naming the folder rather than the file, since the folder is what the person chose
[[nodiscard]] inline std::string dropped_bridge_note(const std::filesystem::path& dropped,
                                                     const script_language& lang) {
    return "left the " + std::string(lang.label) + " bridge in " + dropped.parent_path().string() +
           " unloaded: a process serves one bridge of a language, and a second copy registers the module names the "
           "first already holds";
}

/// Whether a plugin load error is the platform's way of saying "a library this one needs is missing".
///
/// Worth recognising rather than passing through, because both platforms word it about the plugin the
/// host asked for and not about the dependency that is actually absent — "The specified module could
/// not be found" names nothing at all, and a person reads it as "my plugin is broken". What the
/// missing library actually is depends on the language, which is why the advice lives there.
/// @param error the text from plugin_info::error
/// @return true if the message is a dependency failure rather than a plugin failure
[[nodiscard]] inline bool reads_as_missing_dependency(std::string_view error) {
    return error.contains("specified module could not be found") || error.contains("cannot open shared object file") ||
           error.contains("image not found");
}

/// Script file name: the module name whole, plus the language's extension.
///
/// The name is not shortened the way a derived class name is, because a bridge keys its modules by
/// the file stem: two scripts sharing a stem in two directories would shadow each other, and module
/// names are exactly what is already known to be distinct.
/// @param module_name the platform name of the module
/// @param lang the language being authored in
/// @return the file name
[[nodiscard]] inline std::string script_file_name(std::string_view module_name, const script_language& lang) {
    return std::string(module_name) + std::string(lang.file_extension);
}

/// Writes the skeleton of a new module into a directory.
/// @param dir directory to write into; it is created if missing
/// @param module_name the platform name of the module
/// @param lang the language being authored in
/// @return the file written
/// @throws std::runtime_error if the name is unusable, the file exists or it cannot be written
inline std::filesystem::path create_script_module(const std::filesystem::path& dir,
                                                  std::string_view module_name,
                                                  const script_language& lang) {
    if (!lang.name_valid(module_name)) {
        throw std::runtime_error("'" + std::string(module_name) + "' is not a usable module name");
    }
    const std::filesystem::path file = dir / script_file_name(module_name, lang);
    if (std::filesystem::exists(file)) {
        throw std::runtime_error("'" + file.string() + "' already exists");
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::ofstream out(file);
    if (!out) {
        throw std::runtime_error("cannot write '" + file.string() + "'");
    }
    out << lang.render_skeleton(module_name);
    return file;
}

/// A bridge the studio can copy into a module folder, and where it was found.
///
/// `searched` carries the places that were tried, so a failure can say where to look rather than only
/// that it failed.
struct bridge_source {
    std::filesystem::path bridge;
    std::filesystem::path package;
    std::vector<std::filesystem::path> searched;

    [[nodiscard]] bool found() const {
        return !bridge.empty();
    }
};

/// Finds a bridge to copy from: `plugins/` beside the given executable, then the executable's own
/// directory — the two places an installation puts it — and a loaded bridge only as a last resort.
///
/// The installation comes first on purpose, and this order was corrected rather than chosen: a loaded
/// bridge is very often the copy inside a module folder, because the studio makes such a folder a
/// search directory. Preferring it made the folder its own source, so its package could never be
/// refreshed from the platform and a copy one release behind stayed behind forever. What the studio
/// ships is the authority on what the current package is.
/// @param manager the session's module manager
/// @param exe_dir directory of the running studio
/// @param lang the language whose bridge is wanted
/// @return the bridge and its package, or a source whose found() is false
[[nodiscard]] inline bridge_source find_bridge_source(const module_manager& manager,
                                                      const std::filesystem::path& exe_dir,
                                                      const script_language& lang) {
    bridge_source result;
    const auto accept = [&result, &lang](const std::filesystem::path& candidate) {
        result.searched.push_back(candidate);
        if (!std::filesystem::exists(candidate)) {
            return false;
        }
        const std::filesystem::path package = candidate.parent_path() / lang.scripts_subdir / lang.package_entry;
        if (!std::filesystem::exists(package)) {
            return false;
        }
        result.bridge = candidate;
        result.package = package;
        return true;
    };
    if (accept(exe_dir / "plugins" / bridge_filename(lang))) {
        return result;
    }
    if (accept(exe_dir / bridge_filename(lang))) {
        return result;
    }
    if (const std::optional<std::filesystem::path> loaded = find_bridge(manager, lang)) {
        (void)accept(*loaded);
    }
    return result;
}

/// Whether a bridge copy is older than the platform's own, and therefore worth saying so about.
///
/// A copy that is merely different is not stale — only an older one is, and only when both times can
/// be read: a failure to read either is not evidence of age, and reporting it as such is worse than
/// silence, because the advice that follows is "delete this file".
/// @param copy the bridge in question
/// @param platform the bridge the studio ships
/// @return true when the copy is older
[[nodiscard]] inline bool bridge_copy_is_stale(const std::filesystem::path& copy,
                                               const std::filesystem::path& platform) {
    std::error_code same;
    if (copy.empty() || platform.empty() || std::filesystem::equivalent(copy, platform, same)) {
        return false;
    }
    std::error_code copy_failed;
    std::error_code platform_failed;
    const auto copy_time = std::filesystem::last_write_time(copy, copy_failed);
    const auto platform_time = std::filesystem::last_write_time(platform, platform_failed);
    return !copy_failed && !platform_failed && platform_time > copy_time;
}

/// Whether a copy of the package is older than the platform's own, and therefore worth saying so about.
///
/// The twin of bridge_copy_is_stale, and it needs its own guards for the same reason: a package that is
/// not there answers "infinitely old" through newest_write, which is not evidence of age but of absence,
/// and the advice that follows a stale verdict is "delete this". A copy that *is* the platform's own is
/// the ordinary case — a module folder is a search directory, so the loaded bridge is often the studio's
/// — and it is not stale by being itself.
/// @param copy the package in question
/// @param platform the package the studio ships
/// @param lang the language whose package it is, which decides whether it is a directory or a file
/// @return true when the copy is older
[[nodiscard]] inline bool package_copy_is_stale(const std::filesystem::path& copy,
                                                const std::filesystem::path& platform,
                                                const script_language& lang) {
    std::error_code same;
    if (copy.empty() || platform.empty() || std::filesystem::equivalent(copy, platform, same)) {
        return false;
    }
    if (!std::filesystem::exists(copy) || !std::filesystem::exists(platform)) {
        return false;
    }
    return detail::package_is_older(copy, platform, lang);
}

/// The bridge this session actually loaded, when it is older than the one the studio ships.
///
/// Worth asking at every scan and not only when a module is created. A module folder carries its own
/// bridge and the studio never replaces a copy — replacing the file would not change the library this
/// process already loaded, and some platforms refuse the write outright — so a folder provisioned
/// before an update goes on loading its old bridge for as long as that file exists, and
/// whatever the newer platform added is simply absent. That absence raises no error of its own: the
/// modules load and work, they merely lack what the newer bridge would have told the host about them,
/// which reads as a broken studio rather than as a stale file.
/// @param manager the session's module manager
/// @param source where the studio's own bridge is
/// @param lang the language whose bridge is considered
/// @return the loaded bridge when it is the older one, nullopt otherwise
[[nodiscard]] inline std::optional<std::filesystem::path> stale_loaded_bridge(const module_manager& manager,
                                                                              const bridge_source& source,
                                                                              const script_language& lang) {
    const std::vector<std::filesystem::path> loaded = loaded_bridges(manager, lang);
    if (loaded.empty() || !source.found() || !bridge_copy_is_stale(loaded.front(), source.bridge)) {
        return std::nullopt;
    }
    return loaded.front();
}

/// What to say about the loaded bridge being older than the studio's own.
/// @param loaded the bridge in use
/// @param platform the bridge the studio ships
/// @return the line, which has to name both files and the way out
[[nodiscard]] inline std::string stale_bridge_note(const std::filesystem::path& loaded,
                                                   const std::filesystem::path& platform) {
    return "the bridge in use, " + loaded.string() + ", is older than the studio's own, " + platform.string() +
           "; replacing the file would not change the copy already loaded, so delete it and scan again "
           "if modules of that folder behave as if the platform were older";
}

/// The package the loaded bridge reads, when it is older than the one the studio ships.
///
/// A separate question from stale_loaded_bridge and asked beside it, because the two go stale
/// independently and only one of them is visible. A folder keeps its own copy of both; the bridge is
/// never replaced, so it is the obvious suspect, while the package is replaced only by provision_folder
/// — that is, only when somebody creates a module in that folder. A folder merely *scanned* since it was
/// provisioned therefore keeps a package as old as the day it was made, and a script written against
/// anything the platform added since fails inside the interpreter, naming a file in the folder rather
/// than the reason. That is the diagnosis this question exists to shorten.
/// @param manager the session's module manager
/// @param source where the studio's own bridge and package are
/// @param lang the language whose package is considered
/// @return the loaded bridge's package when it is the older one, nullopt otherwise
[[nodiscard]] inline std::optional<std::filesystem::path> stale_loaded_package(const module_manager& manager,
                                                                               const bridge_source& source,
                                                                               const script_language& lang) {
    const std::vector<std::filesystem::path> loaded = loaded_bridges(manager, lang);
    if (loaded.empty() || !source.found()) {
        return std::nullopt;
    }
    std::filesystem::path mine = loaded.front().parent_path() / lang.scripts_subdir / lang.package_entry;
    if (!package_copy_is_stale(mine, source.package, lang)) {
        return std::nullopt;
    }
    return mine;
}

/// What to say about the package in use being older than the studio's own.
///
/// It names a different way out than stale_bridge_note, and the difference is the point: a loaded
/// library cannot be replaced under the process, while a package is only read when a script imports it,
/// so refreshing this one is enough and needs no deletion.
/// @param loaded the package in use
/// @param platform the package the studio ships
/// @return the line, which has to name both files and the way out
[[nodiscard]] inline std::string stale_package_note(const std::filesystem::path& loaded,
                                                    const std::filesystem::path& platform) {
    return "the package in use, " + loaded.string() + ", is older than the studio's own, " + platform.string() +
           "; scripts there cannot use anything the platform added since it was copied, so replace it with "
           "the studio's own — creating a module in that folder does it";
}

/// What provision_folder had to do, so the caller can say it out loud.
struct folder_setup {
    std::filesystem::path scripts_dir;
    bool bridge_copied = false;
    bool package_copied = false;
    bool package_refreshed = false;
    bool bridge_stale = false;
};

/// Makes a folder able to host modules of one language on its own: a bridge in it and the package in
/// the language's subdirectory.
///
/// This is what turns the folder into something portable — the same shape an installation has, so a
/// host pointed at it needs nothing else, and an editor opened on a script beside the package resolves
/// the import.
///
/// The package is **refreshed** when the source is newer, and that is not a convenience: it is
/// platform code rather than the author's, a folder provisioned before an update keeps working only if
/// it follows the bridge, and a package one release behind fails in ways that point nowhere near it —
/// the discovery walk lives there. The bridge file is only ever created, never replaced: overwriting
/// cannot change the copy this process already loaded, and some platforms refuse the write outright,
/// so a stale one is reported instead of half-copied.
///
/// A folder may be provisioned for more than one language, and nothing here objects: the languages
/// differ in every path they touch, and the folder stays one plugin search directory.
/// @param folder the folder the person chose
/// @param source a bridge to copy from
/// @param lang the language being provisioned for
/// @return what was created, refreshed and found stale
/// @throws std::filesystem::filesystem_error if a copy fails
inline folder_setup provision_folder(const std::filesystem::path& folder,
                                     const bridge_source& source,
                                     const script_language& lang) {
    folder_setup done{scripts_dir(folder, lang), false, false, false, false};
    std::filesystem::create_directories(done.scripts_dir);
    if (!source.found()) {
        return done;
    }
    std::error_code same;
    if (std::filesystem::equivalent(source.bridge.parent_path(), folder, same) && !same) {
        return done;
    }
    const std::filesystem::path bridge = folder / bridge_filename(lang);
    if (!std::filesystem::exists(bridge)) {
        std::filesystem::copy_file(source.bridge, bridge);
        done.bridge_copied = true;
    } else {
        done.bridge_stale = bridge_copy_is_stale(bridge, source.bridge);
    }
    const std::filesystem::path package = done.scripts_dir / lang.package_entry;
    if (!std::filesystem::exists(package)) {
        detail::copy_package(source.package, package, lang);
        done.package_copied = true;
    } else if (detail::package_is_older(package, source.package, lang)) {
        detail::copy_package(source.package, package, lang);
        done.package_refreshed = true;
    }
    return done;
}

/// Separator of a scan-path variable, the platform's own: a bridge splits on this character and
/// nothing else, so the studio has to write the same one.
inline constexpr char script_path_separator =
#if defined(_WIN32)
    ';';
#else
    ':';
#endif

/// Composes a scan path: the studio's directories first, the inherited value kept as the tail.
///
/// The tail is kept rather than replaced because a person working on scripts in their own repository
/// starts the studio with the variable already set, and clobbering it would make their modules vanish
/// with no visible cause. Duplicates are dropped by exact string, which is what the entries are — the
/// studio stores what the file dialog gave it.
/// @param dirs the studio's script directories, in order
/// @param inherited the value the studio was started with
/// @return the composed value, without a dangling separator
[[nodiscard]] inline std::string compose_script_path(const std::vector<std::string>& dirs, std::string_view inherited) {
    std::vector<std::string> ordered;
    const auto append = [&ordered](std::string_view piece) {
        if (piece.empty() || std::ranges::find(ordered, piece) != ordered.end()) {
            return;
        }
        ordered.emplace_back(piece);
    };
    for (const std::string& dir : dirs) {
        append(dir);
    }
    for (std::size_t start = 0; start <= inherited.size();) {
        const std::size_t end = inherited.find(script_path_separator, start);
        append(inherited.substr(start, end == std::string_view::npos ? end : end - start));
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    std::string result;
    for (const std::string& piece : ordered) {
        if (!result.empty()) {
            result.push_back(script_path_separator);
        }
        result += piece;
    }
    return result;
}

/// One language's scan-path variable as it stands in the environment right now.
///
/// std::getenv is kept for the same reasons as in settings.hpp: the value is only read, and the read
/// that matters happens at startup, before the application has a second thread.
/// @param lang the language whose variable is read
/// @return the value, empty when the variable is unset
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
[[nodiscard]] inline std::string inherited_script_path(const script_language& lang) {
    const std::string key(lang.path_variable);
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const char* value = std::getenv(key.c_str());
    return value == nullptr ? std::string() : std::string(value);
}
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

/// Puts compose_script_path() into the process environment for one language.
///
/// A bridge reads its variable on every discovery, so this only has to be right by the time one is
/// loaded or reloaded — which is what makes a directory added while the studio runs reachable at all.
/// @param dirs the studio's script directories for this language, in order
/// @param inherited the value the studio was started with
/// @param lang the language whose variable is written
inline void apply_script_path(const std::vector<std::string>& dirs,
                              std::string_view inherited,
                              const script_language& lang) {
    const std::string key(lang.path_variable);
    const std::string value = compose_script_path(dirs, inherited);
#if defined(_WIN32)
    (void)_putenv_s(key.c_str(), value.c_str());
#else
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    (void)setenv(key.c_str(), value.c_str(), 1);
#endif
}

}  // namespace atp::studio

#endif
