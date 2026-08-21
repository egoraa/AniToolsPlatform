# Authoring script modules in the studio

Studio can **author** script modules, not just host them, and it does so for **every language in
`studio/languages.hpp`** — that list is the one place a language is added. A language is a value,
`script_language` (`studio/script_language.hpp`): stem of its bridge, scripts subdirectory, package
entry, file extension, name prefix, scan-path variable, plus three function pointers (name validity,
skeleton, dialog note). Everything else walks the list rather than naming a language, so "we support
two" does not mean two sets of rules kept in step by hand.

`File → New module…` asks for the language first, then provisions the chosen folder into the shape an
installation has — bridge beside it, package and scripts in the language's own subdirectory
(`provision_folder` in `studio/script_modules.hpp`: the bridge file is only created, the package is
**refreshed** when the platform's is newer, freshness measured over the language's own sources alone
because a `__pycache__` written on first import would otherwise make a stale copy look new, and the
copy source is the studio's own `plugins/` before any loaded bridge, since a loaded one is often the
folder's own copy) — writes the skeleton there, makes the folder a **module search directory** and then
either reloads a bridge already loaded (`module_manager::reload_plugin`) or scans so the folder's own
copy loads. A folder may carry **two** languages at once and is still one search directory.

One field carries the whole difference between the languages: **`missing_dependency_hint`** is empty
for Lua, because the interpreter is inside the plugin file and there is no absent runtime to point at.

**`keep_one_bridge` applies to every language**, and it was briefly a per-language flag by mistake —
worth knowing, because the mistake is easy to repeat. One registry plus one search-directory list means
two copies of *any* bridge discover the same scripts and register the same names; `module_registrar::add`
refuses the duplicate, `module_loader` withdraws the file, and a permanent red "failed" row is left in
the dock. CPython has a stronger reason on top — `Ctx` lives in a per-DLL static that only the copy
winning the `_atp` inittab race fills, so its losers could not create modules anyway — but a stronger
reason for the same rule is not a different rule. Every scan is therefore followed by `keep_one_bridge`
for each language; it drops the extras through `module_manager::unload_plugin` (the pair of
`load_plugin`) and names them in the Log, keeping only a failed bridge that has none loaded beside it.

A module may not be named `atp` in either language, by two different mechanisms — `atp.py` beside the
`atp` package replaces it in `sys.modules`, and `atp.lua` *is* the Lua package file. The name checks
differ otherwise and deliberately: Python refuses `_1` because the class it would derive is `1`, and
Lua derives nothing and accepts it.

**A folder's own bridge is never replaced** — a library already loaded cannot be swapped underneath the
process holding it: some platforms refuse the write outright, and where the write does succeed the
process keeps running the code it mapped, so the symptom is the same either way and the remedy is to
delete the copy and scan again. A folder provisioned before an update thus keeps loading the old
bridge with no error of its own — `stale_loaded_bridge` is therefore asked at startup and at every
rescan, per language, and the warning names both files; when a module folder behaves as if the
platform were older, that copy is the first suspect. The build-tree equivalent is
forgetting to rebuild a bridge, which leaves `plugins/` beside `atp_studio` stale — and
`find_bridge_source` looks exactly there first. That directory is now the bridges' own output
directory rather than a copy of it (`ATP_PLUGIN_OUTPUT_DIRECTORY`), so a bridge that was rebuilt is
in place by construction; the two asset targets that used to copy it there are gone. `reload_plugin` is generic (a
rebuilt C++ plugin goes the same way) but **forbidden while the pipeline runs**, since it unregisters
factories the live tree is holding; the window guards that, the core does not. The plugins dock's
rescan button calls `module_manager::reload_all` (every loaded file re-read) **before** `rescan`,
because `rescan` leaves a loaded plugin alone by contract and a bridge reads its scripts only at load —
the two together meant an edited script never reached the palette. There is **no `script_dirs`
setting**: what each bridge is told to scan is derived from the search directories
(`derive_script_dirs`, once per language) and prepended to that language's inherited variable, because
a module folder *is* a plugin folder and two lists would show it twice and then drift; the whole set is
captured and rewritten by `script_environment`, which must be applied for **all** languages at once —
a missed one shows up as modules silently absent from the palette. Settings keys are `editor_command`
(`{file}` substituted) and `last_script_language`.

**A module that declared its config schema edits the config as a tree** (`ui/kit/config_tree.hpp`), not
as a JSON area — but only when its config is an **inline object** or absent. The other three cases stay
on the text editor and are untouched: a module that declared no schema has no types to check against; a
`"file:"` config is somebody else's file in a format the platform may not parse; and a reference to a
shared block belongs to the **document** and may be named by modules whose schemas differ, so drawing it
by one module's schema would let that module rewrite fields it cannot even show. `UiInspectorConfig.*`
(23 tests) is the guard: its module declares nothing and keeps an inline config, so any widening of the
tree drops them all at once.

**The widget holds no rules.** What it shows is `studio::materialise(schema, stored)` — the config with
every declared field present, taking its default where the document said nothing, which is why the tree
can be an editor of an *object* rather than of a schema. What it writes is
`studio::strip_defaults(schema, edited)`. Both are free functions over `atp::config::node` in
`studio/config_shape.hpp`, know nothing about Qt and are tested in `atp_tests`; the pinning assertion is
**`strip(materialise(x)) == strip(x)`**, and it exists because the predecessor kept the same logic inside
the widget and lost data three separate ways.

**Row order in the tree is `materialise`'s own decision now**, and that is new: a `config::node` object
keeps what it was given, so the alphabetical order the old `nlohmann::json` inherited from its `std::map`
is gone. What it writes instead is the declared fields in the order the module declared them, then the
undeclared keys the document held — the order a reader of the plugin's source expects. Saving is
unaffected, since `runtime::json_dump` sorts.

Three rules live in `strip_defaults` and nowhere else. A value equal to its default is not written — the
document must not grow to the full schema because somebody opened a module. A key the schema does not
declare passes through untouched, and unlike before it is also **shown**: the tree walks the object, so a
config written by hand or by a newer plugin is no longer half invisible. A required field holding the
empty value of its type is dropped rather than written, so the module fails with "required and absent"
instead of being handed an empty string — and a **list element is thinned to `{}`** while the array keeps
its length, because the position is the data and an empty element materialises straight back.

**A module config that names a file** (`"config": "file:rig.ini"`, or a shared block that is itself such a
string — the reference is followed before deciding) is shown in the inspector **read-only**
— the source row carries the reference as written and the area below it the content of the file, or the
reason it cannot be read, produced by the very `runtime::load_module_config` the run uses. Studio neither
edits nor rewrites that file: it is somebody else's, in a format the platform may not parse. A relative
path resolves against `app_state::saved_dir()`, which is **empty** for a project that was never saved —
deliberately not `config_dir()`, whose fallback is the directory studio was launched from, so an unsaved
project gets "needs the document's directory" instead of a file read from somewhere nobody meant.
**Emptying the editor clears the module's own config and never the block it named** — deleting a shared
block from one module's panel would leave every other module naming it pointing at nothing, so
`clear_shared_config` has no caller in the GUI at all.
