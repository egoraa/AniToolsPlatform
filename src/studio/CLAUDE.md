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
platform were older, that copy is the first suspect. **`stale_loaded_package` is asked beside it and
is a different question**: the bridge is never replaced, while the package is replaced only by
`provision_folder`, i.e. only when somebody creates a module in that folder — so a folder merely
*scanned* since it was provisioned keeps a package as old as the day it was made. A script written
against anything the platform added since then fails inside the interpreter, naming a file in the
folder rather than the reason (`AttributeError: module 'atp' has no attribute 'Config'` is what that
looks like), and the two notes differ in the way out because the causes do: a loaded library cannot be
replaced under the process, a package can. The build-tree equivalent is
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
by one module's schema would let that module rewrite fields it cannot even show — and a fourth case, a
document holding a value of a form the declaration does not give it, which is below. `UiInspectorConfig.*`
(23 tests) is the guard: its module declares nothing and keeps an inline config, so any widening of the
tree drops them all at once.

**The widget holds no rules, because the object holds them all.** `config_tree` takes its own object of
the module's config type from the very factory that would build the module (`make_config()`), fills it
with `runtime::load_fields`, draws one row per `module_config::entry` and writes back
`runtime::save_fields`. What a default is, which fields exist, what a fresh list element looks like and
what is worth writing down are all questions the object and the binder answer; the predecessor
re-implemented every one of them inside the widget, and losing data three separate ways was what that
cost. `studio/config_shape.hpp` (`materialise`/`strip_defaults`) is gone with them — the invariant it was
pinned by lives on as `ConfigBinding.LoadingWhatWasSavedGivesTheSameDocument`.

The prototype the palette holds is `studio::module_info::config_schema`, a
`shared_ptr<const atp::module_config>` made from the factory's `config_ptr` **with its deleter**: that
deleter carries the plugin pin, and without it a rescan would leave a dangling vtable behind every cached
entry. It is empty for a module that declares no config and for a `runtime::raw_config` — a module that
takes the document whole has no fields to draw and is edited as text. Whether a config reaches the module
at all is the **separate** `module_info::takes_config`, and the pair is what tells "takes none" apart from
"takes one it does not describe": MCP prints the `"config"` key exactly when the module takes one, so an
empty `"fields"` says the module reads what it is given whole — which is what `using config_type =
atp::module_config;` is written for, and reporting it as accepting nothing is how a `"file:"` config that
would have reached it stops being written.

Row order is the order the module declared its fields in, since the tree walks the declarations. Three
rules live in `runtime::save_fields` and nowhere else. A value equal to its default is not written — the
document must not grow to the full schema because somebody opened a module. A required field nobody wrote
is absent rather than written as the zero of its type, so the module fails with "required and absent"
instead of being handed an empty string — which is why emptying such a row erases the key. And a **list
element is thinned to `{}`** while the array keeps its length, because the position is the data and an
empty element loads straight back into the defaults it stood for.

**A field with a declared value set is a drop-down**, never a line to type into: which names exist is the
entry's business (`options()` — an enum's table, or the set the module listed with `allowed()`), and typed
into a line every typo would travel as a refused edit. That covers an element of a list of enums too. A
name outside the set is treated by `config_misfits` exactly like a value of the wrong form, and for the
same reason: `from_string` refuses it, the field stays unset, and the next save would drop it.

A key no field declares has **no row** — there is nothing to draw it from — but it is not dropped either:
the widget carries it through every save, and the problem `load_fields` reports about it is put on the
tree. The document is wrong, and saying so is not the same as deleting it.

**A value the rows cannot read takes the rows away**, and that is a fourth reason for the text editor,
one that comes and goes with the **document** rather than with the selection. `config_misfits`
(`ui/kit/config_tree.hpp`) walks the declaration against the document and answers every field holding a
form that is not the declared one — `8.5` in an integer field, `7` under an array field, a string among
an array of objects — in the very line `load_fields` would say about it. On anything but an empty answer
`config_tree::rebuild`/`sync` refuse, the inspector shows the JSON editor with those lines above it in
`config_problem`, and the rows come back by themselves once the document fits again, which is why `sync`
asks on every change rather than only when the selection moves.

The distinction that makes this work is between a problem that **survives** a save and one that does not.
A required field nobody filled and a key no field declares are both problems, and both are shown while
the rows stand: the first is an empty cell, the second rides through `carry_unknown`. A value of the
wrong form survives nothing — `load_fields` leaves the field unset, `save_fields` writes nothing for an
unset field, and the value is gone from the document the moment anything else on the form is edited. So
"did `load_fields` complain" is the wrong question to lock the form on, and `config_misfits` is not a
second implementation of the binder but exactly that different question. Answering the first would leave
a module with a required field no form at all, since an empty config is "required and absent" from the
start.

**A module config that names a file** (`"config": "file:rig.ini"`, or a shared block that is itself such a
string — the reference is followed before deciding) is shown in the inspector **read-only**
— the source row carries the reference as written and the area below it the content of the file, or the
reason it cannot be read, produced by the very `runtime::load_config_source` the run uses. Studio neither
edits nor rewrites that file: it is somebody else's, in a format the platform may not parse. A relative
path resolves against `app_state::saved_dir()`, which is **empty** for a project that was never saved —
deliberately not `config_dir()`, whose fallback is the directory studio was launched from, so an unsaved
project gets "needs the document's directory" instead of a file read from somewhere nobody meant.
**Emptying the editor clears the module's own config and never the block it named** — deleting a shared
block from one module's panel would leave every other module naming it pointing at nothing, so
`clear_shared_config` has no caller in the GUI at all.
