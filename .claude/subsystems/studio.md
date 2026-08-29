# The studio

## Authoring script modules

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

**`keep_one_bridge` applies to every language**, and a per-language flag is an easy mistake to make
here. One registry plus one search-directory list means
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
in place by construction. `reload_plugin` is generic (a
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

## Editing a module config

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
what is worth writing down are all questions the object and the binder answer — **the widget must not
re-implement any of them**, and there is no `studio/config_shape.hpp`. The invariant is pinned by
`ConfigBinding.LoadingWhatWasSavedGivesTheSameDocument`.

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

**The config is a JSON area in the inspector, not a property row**, read-only while the pipeline runs
like the rest of the structure (via `state_.view->running()`, since `project` knows nothing about a
runner). `project::set_shared_config` exists because otherwise the reference form would be unreachable
from the GUI and a saved reference would not validate on reopening; it also accepts a `file:` string
and still refuses a bare reference. A config that names a file is shown **read-only**, previewed
through the very `load_config_source` the run uses, so an unreadable file says here exactly what it
would say then; studio never edits or rewrites that file.

**Only the project structure is read-only while the pipeline runs.** Property rows stay editable and
saving is allowed, with `sync_persistent_properties` pulling live persistent values into the project on
the fly and dropping those equal to the default. The edit itself goes through `session::set_property`,
the same by-path vocabulary `atp_app -p` uses.

## GUI rules that are easy to undo by tidying

Each of these cost a bug once. The measurement behind it, and the alternatives that were tried, are in
`docs/architecture/studio.md`, in the section named at the end of each paragraph.

**The canvas carries no colour literal** — it paints itself, so it is handed a scheme:
`canvas_colors(const QPalette&)` (`ui/canvas/canvas_palette.hpp`) derives every colour from the widget
palette and each item takes a `const canvas_palette&`. Two things before touching it. Text on the node
body is judged against `node_fill` and text on the empty canvas against `QPalette::Base` — confusing
the two is what left the light theme with unreadable labels. And the canvas repaints **only** from
`canvas_widget::changeEvent`: choosing a theme moves `QApplication::palette()` first and delivers
`QEvent::PaletteChange` to the widget after, so a rebuild called straight from the menu handler paints
the scheme the user just left. Do not add one back. "The canvas palette".

**A node wears a mark beside its name** saying what it is written in — a binary module, a script of
one language or another, a subgroup — and what decides is `module_info::source`, the file the plugin
named the module in: an ordinary plugin names none, a bridge names its script. An extension no
language of `languages()` claims is a script of an unknown language, never a binary module. The mark
is `glyph_item` rather than a `QIcon` for the same reason the canvas is handed a scheme — QIcon's
engine paints in the widget palette's text colour — and its artwork rides on `atp_studio_ui`, not on
the executable, because a missing resource costs no error anywhere. It is placed from the **font's**
metrics, centred on the cap-height band rather than the text item's em box, and trimmed to the ink the
file actually paints. The palette, the project tree and the plugins dock answer the same question
through `icons::module_icons`, so no view disagrees with another about what a module is. "A node's
mark", "The icon family's grid".

**A refusal is a frame drawn over the editor** (`style::mark_error`; the frame itself is
`detail::error_frame`), not a stylesheet border and not a palette tint: a border switches a `QLineEdit`
off native rendering, and a tint compounds on every call and is invisible on a non-editable
`QComboBox`. **`muted()` sets a role, never a resolved colour** — writing `w->palette()` back masks
every role and the widget stops following the theme. **An empty view's note is a label over the
viewport, not a row**: the `UiRuntimeWidget` suite pins "an empty table has no rows" as a contract.
And **`window_state_version` stays 1** although the toolbar is new — measured three ways, Qt leaves a
toolbar absent from a saved state where it is. "Refusal, muting and the empty state", "The shell".

**The Log dock reads as a console**: oldest line first, its four actions in a column beside the list
rather than a row under it, and following the tail releases itself when the reader scrolls away — so
the `valueChanged` handler calls `note_follow_tail` and never `set_follow_tail`, which moves the view
and would close the loop. Following the tail hangs off the scrollbar's **`rangeChanged`**, not off row
insertion: otherwise eviction at `log_model::max_lines` moves the scrollbar, `valueChanged` reads that
as the reader scrolling, and a talkative pipeline unsticks itself from the tail at the ceiling.
**Teardown is stated, not inherited**: `~log_panel` clears each view's `on_follow_tail_changed` and
deletes the views before the model they read, because emptying a list clamps its scrollbar and a view
can report a change from inside its own destruction; for the same reason `current_view()` is nullable
and `sync_follow_button` checks both it and its `QPointer` to the button. **Every line names its
source** — a module by its dotted path, the studio itself as `system` — and `render_log_line`
(`ui/panels/log_entry.hpp`) is the one place a line is drawn. The source is a **kind beside the path**, not a reserved
word inside it, so a module named `system` collides with nothing; the studio's lines *about* a module
(`plugin: …`) are system lines, since they name a type that has no instance yet. **A tab is a saved
`log_query` over one shared `log_model`**, never a history of its own, which is what lets clearing,
eviction and the list of known sources need no agreement between tabs; the first tab shows everything
and cannot be closed, the rest live until the window does and reach no profile. "The Log dock".

Canvas navigation, the grid and the exposed-port stubs are in `docs/architecture/studio.md` alone:
"The canvas: navigation, the grid and exposed-port stubs" and "The shell: toolbar, status bar and dock
layout".
