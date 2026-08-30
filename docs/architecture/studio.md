# The studio (`src/studio/`)

The headless core of the visual editor and the GUI on top of it: the project and its undo, the canvas,
the docks, the icon family, authoring script modules, and the mode for watching somebody else's host.
The document map and the tree layout are in `../architecture.md`.

## The core (`src/studio/`, target `atp_studio_lib`)

The headless logic of the visual editor. `settings` holds the user's settings
(`%APPDATA%/atp_studio` or XDG). `node_ref` is a node's address — a group plus a name inside it — and
`parse`/`full`/`contains` are the one place where dotted-path arithmetic lives: an empty name addresses
the group itself, and `contains` answers "can this node be moved here". `node_lookup` covers
navigation and names (`find_group`, `find_child`, `clone_child`, `unique_child_name`, the checks on a
name and on a port path) — pure questions to the model rather than edits of it, which is why both
`add_module`/`add_group` and the canvas call them. `expose_cascade` holds the rule that a group's alias
is visible only from outside, and everything that follows from it: a vanished alias breaks references
not in its own group but in the parent, and onwards upwards until re-exports stop disappearing
(`take_exports_of`, `cascade_lost_aliases`, `rewrite_alias_above`); these functions do not touch the
history, the caller takes the snapshot. `position_file` is the `<name>.layout.json` sidecar and does
nothing but move positions between the map and the file — not to be confused with `layout`, which
computes them; reading is deliberately lenient, a lost arrangement being no reason not to open a
project.

`project` is the `runtime::config` model plus the operations that maintain the validator's invariants:
undo snapshots through `encode`/`decode`; moving a node between groups (`move_child`, where the
originating group's connections and exposes that referred to the node are broken and recomputed into a
`move_result`, while the thread assignments and positions of the whole subtree are rewritten onto the
new path, and a group cannot be moved into its own subtree); group deletion (`remove_children`, where
the names are checked before the edit, so a bad name does not leave the project half-deleted); and
`edit_scope`, a scope that collapses a compound edit into one undo step, so that deleting a selection
out of connections, exports and nodes stays one Ctrl+Z. `copy_children`/`paste` take a snapshot of the
selection and paste it. Node positions live in the `<name>.layout.json` sidecar, so the config remains
usable by `atp_app`. An `$include` is opened expanded, with a warning flag.

**What stayed in the aggregate, and why:** 19 operations are built as "check → snapshot → edit", that
is, `snapshot()` stands *between* validation and mutation. Extracting such an operation into a free
function is possible only by splitting it in two — otherwise either the snapshot is taken before the
check and a failed edit leaves a spurious undo step, or the function has to know about the history.
Separating validation from mutation is a different design rather than a relocation, so what was moved
out is exactly what does not touch the history.

`clipboard` is a snapshot of the selection: clones of the nodes, the connections between them, and the
subtree's positions and thread assignments by relative path. The buffer holds values rather than paths
into the project, so a cut deletes the source at once and the buffer itself survives undo, a rename and
opening another project; a connection with one end sticking outwards is not copied, and an assignment
to an undeclared thread is skipped on paste. `property_clip` is a separate buffer for one module's
settings, and only explicitly set values are taken, because what was left at its default the user did
not choose; it is applied by name, and a foreign name is skipped with a mention in the log rather than
rejecting the paste entirely. `layout` is the group-level auto-layout, in layers by connection.

`module_manager` covers the search directories and the DLL scan with its ABI handshake. Ports and
properties come from the declared types through `module_factory_base::declaration()`, with no instance
created; a module with no ports node is described by a probe instance, and only there does a broken
constructor make it `broken`. A conflict on (name, version) rejects the whole file. `property_info` is
an alias of `atp::property_declaration` — the name, the `kind`, the default as a string, the set of
options, `persistent`. `module_info::config_schema` is the config prototype, a
`shared_ptr<const atp::module_config>` straight from the factory, empty for a module with no declared
config and for one that takes the document whole: the `config_ptr` becomes a `shared_ptr` together with
its deleter, so the plugin's pin survives the caching of the prototype in the palette.

`config_tree` is the editor of a config **object**, offered only for a module's inline config with a
schema; a reference to a shared block and a `file:` stay text, because the block belongs to the
document and may be named by modules with different schemas. The widget takes its own instance from
the factory, fills it with `runtime::load_fields`, builds rows by walking `entry`, and writes back with
`runtime::save_fields`: it holds no defaults, no zero element and no writing rules of its own. A key no
field declares is not shown as a row — there is nothing to draw — but it is carried through every save,
while a problem from `load_fields` hangs on the tree. A value of the wrong shape gets no rows at all:
`config_misfits` checks the declaration against the document, and on a non-empty answer the widget
refuses and the inspector puts a text editor with those lines in its place.

`session` holds a fresh `pipeline` plus `pipeline_runner` per run, over the manager's registry;
monitoring is the runner's `stats()` and `sample_connections()`, that is, write counters rather than
values; `set_property` edits a live module and `live_root()` returns the live tree or nullptr.
`property_sync` provides `sync_persistent_properties`: before saving while running, the values of live
modules' persistent properties travel into the project, and those equal to a default are removed from
it, keeping the config free of noise; the type is recovered from the `kind`. All of it is tested
without a GUI, in the common `atp_tests`.

## The panels (`src/studio/ui/`, target `atp_studio_ui`)

A GUI on Qt 6 Widgets: the static library `atp_studio_ui` plus the executable `atp_studio` built from
a single `main.cpp` — that is how the GUI is linked by tests (`atp_ui_tests`, a separate target, so
`atp_tests` stays free of Qt).

**It is built when the `ATP_BUILD_STUDIO` option is on** (the default) and Qt6 is found —
`find_package` in `src/studio/CMakeLists.txt`. The option disables **the GUI only**: the headless
studio core and `atp_mcp` are built either way, because they never mention Qt and `atp_tests` covers
them; a gate over `src/studio` and `src/mcp` as a whole would mean the `OFF` configuration simply does
not build.

Inside `src/studio/ui/` the directories are by role — `shell/` (the window and the docks), `model/`
(`app_state`, the drag-and-drop formats), `panels/`, `canvas/`, `kit/` (reusable editors and the
style); the directory root is the include root, so a header is included by a path that states its role
(`"panels/project_tree.hpp"`). The panels are hpp/cpp pairs in namespace `atp::studio::ui` with no
Q_OBJECT and no moc — the widgets are handed an `app_state&` and callbacks.

The project tree shows every group and module at once, with the selection synchronised with the
canvas, nodes moved between groups by dragging, a module or a group accepted from the palette, F2 to
rename, Del to delete, and a context menu with cut/copy/paste — a paste puts the node inside the group
that was clicked, and beside a module. It is rebuilt only when the structure changes, or a click would
destroy the very row a drag starts from. The palette adds a module on a double click and appends the
DLL to `plugins`.

The canvas is a QGraphicsScene of its own: nodes with pins, a rubber-band connection from pin to pin,
Del, a double click to descend into a subgroup, breadcrumbs, positions synchronised with the project,
and a node context menu with cut/copy/paste and Delete. A paste lands under the cursor preserving the
relative arrangement — on Ctrl+V with an offset, and this is the only place that offset lives, the
model knowing nothing about the canvas. On a group node the item becomes "Paste into '<name>'" and puts
the copy inside, as dragging onto it does, and then the original positions are used, because a
coordinate at this level means nothing one level down. On a module node, Copy/Paste properties are
added, carrying settings between instances of the same factory in one undo step; a value is applied
only if the receiver would have accepted it by hand as well — same name, same kind, inside its own set
and not session-only — and otherwise it is skipped and named in the report, because writing it would
create project state that cannot be reassembled in the editor and that only a Run would report.

The inspector covers rename, properties, expose, threads and layout. In the property grid a context
menu copies a row's value, or the whole set as "name = value" lines, into the system clipboard — in the
order they are shown, that is, in declaration order: the registry keeps it and the buffer has no
sorting of its own. The editors hand the menu to the grid (`Qt::NoContextMenu`), or a line edit would
pop its own menu while a combo box would pop none; Ctrl+C and Ctrl+V are deliberately not intercepted
there, the editors needing them.

The module manager is the **"Plugins"** dock (`manager_widget`: the search directories and the plugins
found in them with their load status). The dock is named after what lies in it rather than after what
comes out of the plugins: modules as "the things you put on the canvas" are the neighbouring palette,
and two docks must not compete for one word. The term is exactly the SDK's and the config's
(`"plugins"`, `atp_add_plugin`, `plugin_abi`, the `plugins/` directory) — the shop window and the API
speak alike. The sections inside follow the studio's general rule that a section heading names the
contents of its list (compare `threads`/`modules`/`ports` in Runtime): `search directories` and
`found plugins`, where the second name distinguishes the list from the first and does not sound
tautological under the dock's heading.

The Runtime dock has Run/Stop and busy% from deltas, plus an `updated <time>` label to the right of the
status: the panel's timer ticks **only while something is running**, so after a stop the tables keep
showing the last numbers, and the frozen stamp is the only thing naming the moment they stopped being
true.

The **"Log"** dock (`log_panel`) is the one panel with no `app_state` and no callbacks — it is simply a
view over lines: multiple selection, Ctrl+A, Ctrl+C, and a Copy/Select All/Clear menu putting the
selection into the system clipboard in the order the lines are shown, since `selectedItems()` answers
in selection order. `append(text, level)` colours a line by level — an error red, a warning amber, and
everything else in the palette's colour, because colouring everything means highlighting nothing. The
colour lives in the widget rather than in the caller: it is a property of the view, and what goes to
the clipboard is text the level already names in words. The dock is called "Log" rather than "Errors":
every level goes there, and naming it after the worst of them would mislead. **The line's shape is one
for all** — `time [level] text`, with a path between the level and the text for module lines: `report()`
assembles it itself from `format_log_time` and `level_name`, while module lines arrive ready from
`format_log_line`. Hence a rule: a message **does not name its own level in words** — the bracket has
named it, and a "warning: …" inside the text would be a second name for the same thing. All for one
shape in a column that is read down rather than line by line.

Monitoring is a QTimer at 10 Hz: connections highlighted by the growth of the write generation. There
are no values there and there cannot be — see `sdk.md`, "Why there is no cache". The GUI thread owns
the session; while the pipeline runs the project's **structure** is read-only but the properties are
not: their rows stay available (Set writes into the live module and, if the property is persistent,
into the project; Reset writes the default to both), and saving is allowed too and pulls the live
values in through `sync_persistent_properties`. A row's editor is chosen first by the set (a non-empty
one gives a combo box regardless of `kind`) and then by `kind`: boolean gives a checkbox, anything else
a line edit.

## The canvas palette (`ui/canvas/canvas_palette.hpp`)

The studio's panels are built from ordinary widgets and take their colours from the style; the canvas
does not. It draws rectangles and text on a `QGraphicsScene`, so every colour of it is chosen by
itself, and the scheme has to be **handed** to it. With the colours as literals tuned for the dark
theme, the light one left dark node plates on a white sheet, and the labels of exposed ports were drawn
in the node's ink over the canvas background at a contrast of 2:1 — that is, unreadable.

`canvas_colors(const QPalette&)` is a **pure function**, and that is its main property: the contrasts
are checked by a test (`tests/ui/canvas_palette_tests.cpp`) against the WCAG formula, in both schemes
at once and with no live scene. The threshold there *is* the requirement: if it does not hold, the
mixing coefficient moves, not the threshold. The scheme is read from `QPalette::Base` rather than from
`Window`: the scene lies on `Base`, and that is what the labels have to survive on.

**The two grounds are named separately, and that is not pedantry.** What lies on a node's body — the
name, the factory, a port's name — is measured against `node_fill`; what lies on the empty canvas — an
exposed port's alias, a connection, a monitoring value — against `Base`. Failing to distinguish those
two is exactly what the bug rested on.

Three colours are not derived from the palette, each for its own reason.

**A pin's outline is the node's own colour**, so that a pin reads as a hole in the node's body through
which the type's colour shows. The first idea was a fixed dark circle ("a pin is always bright, and one
dark outline separates it from any ground"), and a parameter sweep showed that in the light scheme no
such set exists at all: a colour giving 3:1 against a pale node has to be dark, and then it does not
give 3:1 against a nearly black outline. An outline in the ground colour removes the second requirement
entirely, leaving one: the pin's fill must be visible both on a node and on the canvas.

**The module hue and the group hue are fixed** (225° and 260°): the difference between them is meaning
rather than decoration, and deriving it from the palette would make it depend on the theme.

**The "no factory" red is derived separately from `style::error_ink`.** That constant is tuned against
a panel's background; the same red on a node's dark body falls below three to one, and four and a half
is needed — it is text.

**A type's colour is built in HSL, not HSV.** At a fixed `value` a blue pin is three times darker than
a yellow one, and no pair of grounds serves every hue at once — the check failed at 240°. At a fixed
`lightness` all hues are equally visible; hence the field name `type_lightness`.

**Delivering the scheme has exactly one route, and it is `canvas_widget::changeEvent`.** This is easy
to get wrong, and it was got wrong: under the `offscreen` platform the tests run on, changing the
scheme through `QStyleHints::setColorScheme` delivers nothing to the widget — there is simply no
platform theme there — and from that the wrong conclusion was drawn, that the widget had to be told
directly from the menu handler. A trial on live Windows showed the opposite order: choosing a theme
changes `QApplication::palette()` first, and `QEvent::PaletteChange` reaches the widget **after**. A
rebuild called straight from the menu handler therefore managed to read the widget's palette while it
still held the scheme being left, and drew the canvas in it; the right colours appeared only on the
next step, when the event arrived. Such a call is gone: waiting for the event is what gives the correct
scheme.

`canvas_widget::refresh()` re-reads the palette itself, so that **any** rebuild happens in the current
scheme, whatever caused it. The scene is rebuilt whole rather than recoloured in place — that is what
it does on every change to the project anyway, and a second way of saying the same thing would be
redundant.

## The canvas: navigation, the grid and exposed-port stubs

Zoom belongs to the **view** rather than the scene and lives in `canvas_widget`: `zoom_in`/`zoom_out`
step by a ratio of 1.25 rather than an increment, so "zoom out" exactly undoes "zoom in"; the range is
clamped to 0.125…8. The wheel and the middle button are caught by an **event filter on the viewport**
rather than by an override in the widget — those events go to `QGraphicsView`'s viewport and would
never reach the widget itself.

Three things here cost mistakes and are written down so as not to cost them again.

**Zoom is Ctrl plus wheel.** The bare wheel is left to the view and scrolls. It was the other way round
at first, after the node editors, and Ctrl+wheel — the reflex of any editor — landed in scrolling. A
wheel event with no vertical component (a sideways trackpad swipe) is **passed on** rather than eaten: a
branch that did nothing is obliged to return the event.

**Panning moves the scrollbars by hand.** `QGraphicsView::ScrollHandDrag` is handled for the **left**
button only, so setting that mode for the middle one turns the cursor into a hand and scrolls
nothing — which looks exactly like broken panning.

**`zoom_to_fit` temporarily switches the anchor to the view's centre.** `setTransform` under
`AnchorUnderMouse` re-centres on the point under the cursor, undoing the centring `fitInView` has just
done.

**The grid is the only thing on the canvas with a contrast ceiling rather than a floor.** It exists to
give the eye a sense of distance, and the moment it becomes as noticeable as a connection it starts
competing with the content it measures; the test `TheGridStaysQuieterThanAnythingDrawnOnIt` keeps it
below 1.6:1 against the background. Below a zoom of 0.4 it is not drawn at all: the lines converge
closer than the pixels that would carry them, and what reaches the screen is grey mush instead of a
measure. It is drawn over the exposed rectangle rather than the whole scene, so the cost is
proportional to the window rather than to an infinite scene.

**An exposed port's stub is a short segment by its own pin**, and that is a choice rather than
something unfinished. A variant with a "rail" beyond all the nodes of a level was built and rolled
back: it guaranteed the label would not land on a node, but it stretched a dashed line across the whole
level, and labels on a shared rail began to overlap one another — they had to be spread vertically, and
together that read worse than the small overlap it was fixing.

What remains from the rail and must remain: **`shape()` is narrowed to the head and the label**, while
`boundingRect()` unions it with the segment. The first is needed because a rubber-band selection around
a node would otherwise capture a foreign stub, and Delete would remove that port's export in the same
undo step without saying a word. The second, because `QGraphicsPathItem` derives its rectangle **from
`shape()`** when the pen has width: narrow one and you narrow the other, and then a node dragged by the
mouse leaves the old line on the canvas, while scrolling that takes the head off the edge removes the
line entirely.

**A stub lights up from a connection outside it.** Monitoring highlighting is addressed by the pair
(group path, connection index), and a stub is not a connection: the port it marks is the end of a
connection declared in the enclosing group, and that group counts the writes. From inside the group the
flow through an exposed port is visible nowhere else, so a stub that does not light up is the one place
where a running pipeline looks stopped. `outer_connection` therefore goes **upwards** rather than one
level: a port re-exported by the parent carries the connection further out, and stopping at the parent
would leave such stubs dead. It answers with **all** the connections rather than the first found:
several outputs write into one input, and a stub bound to the first named would stay dark while the
second was delivering through it — exactly the failure the mark exists against. The dashes are kept —
a hot stub must not read as a connection — and the port's type colour stays visible on the pin the
segment starts from. `prev_writes_` is therefore keyed by a pair rather than by an index alone: the
scene compares write generations for a foreign level too.

**A first reading is a count, not growth.** A key not present in `prev_writes_` is not highlighted at
all: there is nothing to compare against. Comparing with zero would light up, on entering a running
pipeline's subgroup (a rebuild clears `prev_writes_`), everything that had ever written, in one frame,
although nothing arrived. At a 10 Hz poll the honest rule costs 100 ms of delay on the first frame and
removes the false flash both there and on the first poll after a start.

**Selection survives a rebuild.** `rebuild()` seeds the restore list from the **scene's** selection at
the start, unless somebody has set one. Without that, every rebuild — that is, every change to the
project — left the project with the name of the selected node, so the inspector went on editing it
while the canvas selected nothing and Delete, which walks `selectedItems()`, did nothing. The names are
taken from the scene rather than from the project: the project remembers **one** name, and a selection
of three would come back as one. A rebuild that has already been told what to select (a paste, a
copying drag) is not touched.

**A grabbed node rises above the rest.** With stacking in document order, a node dragged out from under
a neighbour stays under it — a gesture visible nowhere except the very place it is made. So the press
that raises a node, and the whole selection with it, is remembered by the scene and handed out to the
nodes as z. Three decisions inside.

The order is remembered as **a list of names per level** rather than only as z set on the items: the
scene is rebuilt whole on every change to the project, so stacking that lived only in the items would
return to document order on the first edit of a property. That list deliberately does not reach the
`.layout.json` sidecar: a node's position is worth storing always, while "who is on top" only lasts
while the pile is being sorted out.

The z band is **negative**, counted from -1 downwards, so that connections and stubs, which have no z
of their own, stay above any node however high the pile grows. A positive band would need a ceiling put
on them, and a ceiling is a number you can reach.

Among themselves the raised nodes keep the relative arrangement they had: raising a selection of three
lifts the stack rather than reshuffling it.

**A new node is placed above the raised ones**, and `rebuild()` decides that rather than whoever
created the node: the scene remembers what the level contained in the previous build and raises
everything that was not there. Otherwise the raise would have to be written into every way of making a
node — a drag from the palette, a double click in it, Ctrl+V, "new group", a copying drag, a rename, an
undone deletion — and three of those paths never reach the scene at all (the palette and the project
tree hold `app_state` and `ui_callbacks`, but not the scene). The first build of a level raises nothing
and only remembers the membership: to a scene that has not drawn this group yet everything is new, and
writing the whole membership into the raised set would freeze today's document order, after which the
document could no longer say anything about it. The picture is unaffected — the last child is on top
either way.

Stacking belongs to **the project on screen**, so `forget_stacking()` is called wherever the project is
replaced whole: opening, "new", attach, detach, and refreshing the mirror, which builds a project from
a fresh description and lays it out again. Without it a node whose name matched a raised one in a
previous project would end up on top for no reason visible to its author.

A press with Ctrl **does not raise** the node by itself, although it does collect `moving_`: until
release it is not known whether this is a copying drag or a click on the selection, and a click can
also be a deselecting one — nobody grabbed a node struck out of the selection. `begin_copy_drag()`
raises the copies, once the drag has actually begun.

**Fit to Window** and **Actual Size** are in the `View` menu and in the context menu of empty canvas
alike — and while the pipeline runs as well: zoom has nothing to do with the structure being locked.
The scene does not know its own zoom, so it was given two `std::function`s set by `canvas_widget`; they
are deliberately not routed through `ui_callbacks`, which carry what happened to the **project** rather
than how it is being looked at.

## A node's mark: what the module is written in (`ui/canvas/canvas_items.hpp`, `ui/kit/icons.hpp`)

Beside a node's name stands a glyph: a binary module, a script in one language or another, a subgroup.
What tells them apart is **the file the module is declared in** — `module_info::source`, which the
plugin reports to the loader — and nothing else: an ordinary plugin names no file, a bridge names its
script. Two rules follow, both visible on the canvas. The extension is matched against `languages()`
rather than against a list written here, or a language would be added in two places instead of one. And
a module that named a file with an extension nobody knows is a script in an unknown language rather
than a binary module: it gets the generic `script.svg` sheet, a binary box in its place being a mistake
visible only in the picture.

The path is parsed **as bytes**, without `std::filesystem::path`: on Windows that conversion goes
through the process code page and throws on a name it cannot represent, and a module folder named in
Cyrillic must not cost the canvas its mark. Case is folded over ASCII only: the extension is ASCII
anyway, and `towlower` would answer differently under a different locale.

The glyph is drawn by `glyph_item` rather than a `QIcon`, and the reason is colour. The icon family's
engine paints artwork in the colour the widget's palette gives text; on a panel that is the right
answer, on a node's body the wrong one, because everything there is judged against `node_fill`. So the
item is handed **both the artwork and the ink** (`canvas_palette::node_title`) and asks the application
for nothing. The tint is done with `CompositionMode_SourceIn`, which needs a surface of its own, so the
SVG is rendered into an image — at the resolution the painter actually works in, `devicePixelRatio`
included — and the image is kept until the size changes. A pixmap computed once would be the only thing
on the canvas that blurs on zoom, when everything else is vector.

The mark is aligned by **font metrics** rather than by a constant, and that is two separate
corrections, each measured. First: the mark cannot be centred in the text item's rectangle — that is
the em box, taller than the letters and asymmetric about them, so the mark sits noticeably above the
line (measured on a rendered canvas: the mark's centre at 12.4 against the cap centre at 13.1). It is
centred on the band the capitals occupy, from the baseline up by the cap height. Second: marks of one
family differed in ink height by half again (7.25 px against 11.75 for the same 14 px box), because the
folder, the box with ports and the sheet of paper were each drawn to their own scale. So a mark is
**cropped to its own ink**: the artwork is measured once — what it actually paints, not what its
`viewBox` declares — and drawn so that this measurement fills the square, preserving proportions. That
is also what makes the promise "just drop in a `<language>.svg`" real: whoever draws the next mark need
not guess this family's margins.

The same rules for choosing a mark apply in the palette, the project tree and the plugins dock, through
`icons::module_icons`, which holds one icon per kind and answers by the same `module_info::source`.
Holds rather than creates each time: a family icon parses its artwork when constructed, and the panels
are rebuilt whole. There is no ink cropping there — in menus and panels the file is taken as it is.

The artwork lives in the resources of the **library** `atp_studio_ui` rather than of the executable:
the code that reads it is compiled into the library, and `atp_ui_tests` links exactly that. While the
resources sat on `atp_studio`, a missing file would have cost no error at all — `QSvgRenderer` simply
draws nothing — and the suite would have been green over a canvas with not one mark. That is why
`glyph_item::valid()` exists and is checked by a test.

## The icon family's grid (`ui/resources/icons/`, `ui/kit/icons.cpp`)

The family is drawn by two rules, and both are measured rather than chosen by eye.

**Integer coordinates and an even pen.** Only the pair works: a stroke of width 2 placed on an integer
coordinate puts both its edges on integers too, so at the 24 px the toolbar asks for the line covers
whole pixels — and goes on covering them at 150 % (a coordinate ×1.5 lands on a half, the pen becomes
3, the edges return to integers) and at 200 %. A pen of 1.8 on coordinates like `2.5` and `6.6` puts a
vertical's edge in the middle of a pixel, and the rasteriser spreads it over two columns with partial
coverage — that is what "the icons are blurry" is. Curves are antialiased either way; the impression
comes from the straight lines.

**The drawing fills the box `3 … 21` on its long side**, that is, with the pen, ink from 2 to 22, five
sixths of the grid. This is the other half of "one style": without it a glyph ten units wide stands on
the toolbar in the cell next to an eighteen-unit one. The measurement is recorded by the test
`TheWholeFamilyIsDrawnOnOneGrid`, and what it measures is the **ink** rather than what the `viewBox`
declares: the long side's fraction has to fall within 0.72…0.86, a band around the common 20/24 =
0.833. A band rather than one number, because one file departs for a reason.

That file is `stop.svg`, a 16-unit square instead of an 18-unit one: a square covers its box entirely,
a triangle half of it, and an outline less still, so a stop at the family's size reads heavier than the
run standing beside it. `python.svg` has a different exception in a different place — a pen of 1.6
instead of 2: its mark is two interlocked bodies where the others have three or four strokes, and at
the common weight it carries noticeably more ink than its neighbours. A node's mark must not pull the
eye across the canvas to whichever node happens to be a Python one — the same reason it is an outline
rather than a silhouette. Its size is the common one: the drawing is fitted into the box by a scaling
group rather than by a hand-picked `viewBox`, so that the `0 0 24 24` grid stays one for the whole
family.

**The icon engine draws at the destination's resolution, not the logical one.** `QIcon::paint` — the
route both a toolbar button and a menu row take — hands the engine a rectangle in logical pixels.
Drawing an image of that size and letting the painter's transform stretch the result means blurring
every icon on a scaled display, and that is exactly what happened here while the canvas, which always
accounted for `devicePixelRatio`, stayed sharp. The density is asked of the paint device — as Qt's own
`QSvgIconEngine` does.

## The shell: toolbar, status bar and dock layout

The toolbar declares no action of its own: everything on it is already in the menus, and it merely
brings what the hand reaches for to within one glance of the canvas. There are five groups — file,
edit, structure, transport and host — and the last is a single `Attach to a running host...`:
connecting to somebody else's process is not transport, it starts and stops nothing here, so it stands
behind its own separator rather than right after `Stop`. The rest of the `Host` menu is deliberately
not on the toolbar: detaching, refreshing the mirror and shutting down a remote host are things done
having already opened a menu, not things the hand reaches for over the canvas.

**Attach is disabled while your own pipeline runs** (`!attached && !locked`), and that is not caution
but a hole which putting the button on the toolbar made reachable in one click. `app_state::attach`
does not stop a local run: it substitutes `view` with the remote one, after which `refresh_all` reads
`locked` from the foreign host already, and `run`/`stop` are disabled anyway by `!attached`. The result
is your own pipeline spinning with nothing in the window to stop it, right up to `Detach`. In a menu it
was the same hole, but behind a menu; beside `Run` and `Stop` it invites the click. Recorded by the
test `AttachIsRefusedWhileTheLocalPipelineRuns`.

The groups are separated by ordinary `addSeparator()` calls — spacers on either side of a separator
were tried and push the groups too far apart; what a separator looks like is the style's decision.
Starting and stopping are lifted out of the dock buttons' lambdas into the public
`runtime_widget::start_run`/`stop_run`, or the toolbar and the button would be two copies of the same
sequence and loading the plugins could be forgotten in one of them.

The status bar shows the pipeline's state and the project's path. The state is shown twice — here and
in the Runtime dock — on purpose: a dock closes, and the answer to "is it running?" must not. The path
label carries `QSizePolicy::Ignored`, or a long path would impose a minimum width on the whole window
through `sizeHint`; the full path is duplicated in the tooltip.

**`window_state_version` is 1, and that is checked rather than forgotten.** The arrival of the toolbar
looks like a reason to raise it — a saved blob wins over the code, and the fear that the toolbar will
vanish for anyone who has already run the studio sounds convincing. It was measured three times: by an
isolated test with the studio's full set of docks
(`AProfileSavedBeforeTheToolbarExistedDoesNotSwallowIt`), by the same test in a simplified form, and by
the live application with an old profile. In every case `restoreState` returns `true` and the toolbar
stays visible — Qt does not hide a toolbar that is absent from the stream, it leaves it where it is by
default. On top of that it is in the built-in dock menu on a right click, so a way to turn it back on
exists in the UI as well. Raising the version means throwing away everyone's saved layouts for a
problem that does not exist.

The default layout gives the bottom row a fraction of the window's height rather than a fixed number of
pixels. It is computed in `size_docks()`, extracted from `apply_default_layout()` and repeated after
`restore_layout()` when that **returned false**: the constructor lays the docks out before the saved
geometry is applied, so the fraction there would be computed against the wrong window. The gate is on
`restoreState`'s result rather than on "the string is non-empty" — a corrupt or outdated blob would
pass the second check and fail the first.

**A fraction only holds while nothing in the row insists on pixels.** `resizeDocks` is a request, and
a dock is never resized below the minimum its content declares, so a panel asking for a number of
pixels wins over the layout without saying anything. The Runtime panel asked: it stacks three tables —
threads, module metrics, port metrics — and their minimums, the section headers and the controls row
came to 438 px against the 300 the fraction gives the bottom row of a 900-pixel window. The row took
the difference from the canvas, which is how the thing being edited ended up with less room than the
docks it is edited between. The panel's body therefore sits in a `QScrollArea`, exactly as the
Inspector's does: the dock's minimum drops to 87 px, the row keeps its fraction, and the section that
does not fit is reached with the scrollbar rather than every table being squeezed to a header and a
row. Squeezing them was tried first — a floor of a header and a row instead of `style::embed_view`'s
four rows — and it does satisfy the invariant, at the price of three tables clipped mid-placeholder in
the default layout. Recorded by the test `TheDefaultLayoutLeavesTheCanvasMoreRoomThanTheBottomDocks`,
which reads the two heights at that window size.

## The Log dock: line order, the side strip and sticking to the tail

**A log reads like a console — old at the top, new at the bottom.** The reverse order works in itself,
but only until the first button: "to the end", "follow the tail", "scroll down" — the entire vocabulary
people use about logs points downwards. The order is chosen so that a button's name and what it does
agree without a caveat.

**The strip of actions stands to the left of the list, not below it.** Below the list it was a single
button at the dock's very bottom edge, where it cannot be told from the window frame and there is
nothing to explain why it is there. The point is not the button but the place: in a dock whose whole
body is one list, a row along the bottom takes a line away from the text and merges with the edge,
while a column at the side occupies width the text was not using anyway and stands right next to what
it acts on. `style::make_button_column` is the same `button_bar` stood on end, and their margins and
spacing are shared so that the strip does not look like two different strips in two docks.

**The strip carries icons although every other strip in the panels carries glyphs.** Two of the three
actions — wrapping and following the tail — have no letter shape: they are drawn, not written. And a
strip where one button is a glyph and the two beside it are drawings reads as two different sets placed
side by side. So the exception is made for the whole strip, clearing included.

**Following the tail does not stick against the reader.** Scrolling up turns it off by itself and
scrolling back down turns it on; what decides is `at_end()`, the scrollbar's position rather than a
line count, so an empty log is "at the end" too. Without this, following the tail would yank the text
out from under someone reading what is already written — the only thing a log exists for. The mechanism
is simple and has one trap in it: the `valueChanged` handler calls `note_follow_tail` rather than
`set_follow_tail`, because the latter moves the view itself, and calling it from a handler for the
view's movement is a loop.

**Only what was asked for reaches the profile.** A button was pressed — the state is saved; the
scrollbar slid — it is not. The second is a position in a list rather than a decision, and recording it
would mean rewriting `settings.json` on every turn of the wheel. In that case the button is
synchronised by the `on_follow_tail_changed` callback under a `QSignalBlocker`, so the `toggled` signal
does not fire and no save happens. A callback rather than a signal, because there is no `Q_OBJECT`
anywhere in this layer.

**A line is never elided in any mode** (`Qt::ElideNone`). With wrapping off, a long line is reached by
scrolling horizontally; an elided one cannot be reached at all, and a log exists so that a message
reaches a bug report whole.

**Every line names its source, and one function draws them.** Two renderings in two layers — one for a
system line, one for a module line — diverge by exactly the field that is missing: the module one names
the instance path, the system one names nothing, and by eye a system line differs from a module line
not by a mark but by its absence. So they are drawn by one function, `render_log_line`
(`ui/panels/log_entry.hpp`), for both cases: `14:23:05.123 [warning] stage.counter: …` and
`14:23:05.123 [info] system: …`. There is nowhere for them to diverge.

The source is **a kind beside a path** (`log_origin` plus a string) rather than a reserved word inside
the path. The difference shows where a top-level module is named `system`: it will be drawn as a system
line, but the filter and the tabs will not confuse them, because they compare the kind. The studio's
lines **about** a module — `plugin: …`, `new module: …` — are system lines, and that is no compromise:
they speak about a kind of module that has no instance yet, so they cannot have a source.

**A tab is a saved query, not a history of its own.** One `log_model` sits under every view, a tab is a
`log_view` with its own `log_filter`, and a `log_query` of two fields (the writer's kind and a path)
expresses "the whole log", "modules only" and "this instance only" alike. One history is why clearing,
eviction and the list of known sources need no agreement between tabs: there is nothing to agree about,
there being one of each. The first tab shows everything and has no close cross; the rest open on demand
and live until the session ends, so they never reach the profile. A tab whose query is already open is
raised rather than opened a second time, and what decides that is a comparison of **queries** rather
than of titles, because two titles may legitimately coincide.

**The tab strip is visible only from the second view on.** A single "All" tab over a dock whose whole
body is that very list says nothing and takes a line away from the text — exactly the argument by which
the button strip stands at the side rather than below. It appears with the second tab and disappears
with it.

**The close cross is drawn by a proxy style, and that is the only place to reach it from.** The stock
indicator is red under the dark theme — the very colour an error is marked with in this same log, spent
on leaving a tab — and it sits far from the edge: `QCommonStyle` gives it a margin derived from
`PM_TabBarTabHSpace`, which is half of a tab's not-small letter spacing. Neither the colour nor the
place is set by the widget, and two attempts to do it from the widget — a `QToolButton` of the panels'
family set on the tab as `tabButton` — solved neither: the style placed it by its own margin anyway,
and a glyph in a button read as a stray sign beside the caption. `tab_close_style : QProxyStyle`
answers both: `PE_IndicatorTabClose` is drawn with the family's artwork `icons::close_tab()` (a line in
the text colour, at the family's weight, that is, quieter than the caption), and
`SE_TabBarTabRightButton` is shifted to the tab's right edge.

Two traps, each of which cost a run. The cross is drawn **directly in `drawPrimitive`** rather than
handed over as `SP_TabCloseButton`: `QCommonStyle` caches that icon inside the **application's** style,
shared with every other tab bar in the window, and whoever asks first fills it. And the style has to be
set **on the indicator itself, not only on the strip**: `QTabBar` does not draw the cross — it is a
separate child widget — and a widget's style is not inherited from its parent, so a proxy on the strip
alone lays the indicator out and leaves the application's style to paint it. The proxy is a child of the
panel and does not hold its base, so it follows a theme change by itself.

**The cross's size is measured against the dock's close button, not against the indicator it is
given.** The style receives the indicator at `PM_TabCloseIndicatorWidth`, which is 16 px, exactly a
small icon — while the dock's own close button beside it draws its cross at 10 px inside a 20×20 field,
that is, `0.6 * PM_SmallIconSize` by Qt's rule for dock title buttons. Artwork stretched across the
whole indicator gives about 12 px of ink against the neighbour's 7 (measured on the windows11 style at
a scale of 1.5), and the tab looks as though its cross mattered more than the whole panel's. So the
drawing is shrunk to the same fraction while the rectangle stays as it was: it is the mouse target and
the place hover highlights, and shrinking it after the artwork would trade hit accuracy for looks.

**The history is bounded** (`log_model::max_lines`, 20 000 lines, oldest evicted). A log worth
filtering is a log whose history outlives a run, and an unbounded history leaks with a talkative module.

**Following the tail hangs on the scrollbar's `rangeChanged`, not on a line being inserted.** That is
the answer to eviction rather than a detail: removing lines from the top moves the bar, the
`valueChanged` handler reads any movement of it as "the reader scrolled" and turns following off — so a
talkative pipeline would detach itself from the tail at exactly the moment it hits the ceiling. Both
insertion and eviction change the **range**, so a following view leaves for the end from there, before
the offset can be read as a decision. No "eviction in progress" flag is needed at all, and
`valueChanged` still calls `note_follow_tail` rather than `set_follow_tail` — the same trap and the same
reason. One case stays inexact deliberately: an eviction happening while the reader stands almost at the
end will press the view to the end and turn following back on. That is worth a line here and not worth a
mechanism.

**The strip's buttons are of two breeds.** Wrapping is a property of the dock: turn it on and every
view wraps, including ones not yet opened; it is also what lies in the profile. Following is a property
of a view, because scrolling turns it off and it is a particular tab that gets scrolled. Hence
switching tabs synchronises the button under a `QSignalBlocker`: otherwise a view coming into sight
would reach the profile as the user's decision — the same way `on_follow_tail_changed` already prevents.
Clearing clears the **model**, that is, the whole history: the tabs remain but all of them empty at
once.

**The panel's teardown is described in full rather than inherited from Qt.** The order here is
non-obvious three times over: the model is a member, the views are children, and the button strip is a
child too, added to the layout first. Without a destructor of its own the members go before the
`QWidget` base deletes the children, and every `log_filter` looks at a destroyed source model until the
end of teardown. So `~log_panel` first removes each view's `on_follow_tail_changed` callback, then
deletes the views, and only then releases the model. Removing the callbacks is not belt and braces:
emptying a list presses its scrollbar, that is, a view is capable of reporting a state change from
inside its own destruction, and the handler holds a panel whose `views_` is already empty. It does not
fire today only because `std::function` is a member of the heir and dies before the scrollbars of its
base; nobody promised that order. For the same reason `current_view()` returns `nullptr` rather than
indexing the vector by an unchecked `stack_->currentIndex()`, and `sync_follow_button` checks both that
and its `QPointer` to the button.

**The Qt floor is what the GUI is actually built with, and it is deliberately below `ATP_QT_VERSION`.**
A distribution's own Qt build is a legitimate whale, and `ATP_AUTO_INSTALL_QT` is off by default, so
every step of the floor upwards silently takes the GUI away from whoever builds against a system Qt:
`find_package` finds nothing and `src/studio` skips `atp_studio`. Two consequences. The call the floor
is wanted for is worth reconsidering first — `log_filter::set_query` tells the proxy model the same
thing through `invalidateRowsFilter()` (Qt 5.11) that it told through the
`beginFilterChange()`/`endFilterChange()` pair, and that pair alone would have cost the tree 6.10. And
the skip message is obliged to distinguish "no Qt" from "Qt older than the floor": the second reader is
otherwise told to look for an installation they already have. The reason comes from
`Qt6_CONSIDERED_VERSIONS`, config mode's own record of what it rejected — and that matters more than it
seems: a second `find_package` without a version would answer the same question but would cache
`Qt6_DIR` on the very kit just rejected, where the `REQUIRED` call of the auto-install would find it.

## Refusal, muting and the empty state (`ui/kit/ui_style.hpp`)

**A refusal is marked by a frame drawn over the editor** (`detail::error_frame`), transparent to the
mouse and resized with it. Three mechanisms were tried, and only this one is simultaneously idempotent,
theme-following and visible on any editor. A stylesheet — the original variant — turns off
`QLineEdit`'s native rendering entirely. Tinting `QPalette::Base` breaks in two ways at once: it reads
the **current** palette, so a second marking mixes red into an already tinted background and the field
walks towards solid red; and it is invisible on a non-editable `QComboBox`, which under Fusion is
painted from `Button`. The frame's colour is read **at paint time** rather than written into the
widget.

From the same family of mistakes: `muted()` sets the **role** `QPalette::PlaceholderText` rather than a
resolved colour. Writing `w->palette()` back sets a mask on **every** role, and the widget stops
following the theme — a muted caption stays grey from the scheme it left. `error_text` is forced to
store a value, there being no "refused" role in `QPalette`, and therefore re-reads it on a scheme change
through `error_ink_keeper`, the same device as `indent_keeper` beside it.

**An empty view's placeholder is a `QLabel` over the viewport, not a row in the model.** A row would
break the contract pinned by tests that an empty table has no rows, and on top of that would land in
selection, copying and sorting. The viewport's layout is given `SetNoConstraint`: otherwise it imposes
a minimum size on a widget whose size the scroll area itself manages, and a narrow dock starts clipping
the table instead of scrolling it.

**The module palette and the Plugins dock are not merged and must not be.** From a screenshot the merge
looks obvious — the lists of modules in them coincide — but the panels answer different questions, and
only the second carries the search directories, the load status and the "copy path"/"open script"
menu. The real shortage was that a module cannot be found in a list of a hundred: the palette gained a
filter. An empty filter hides **nothing** — a plugin that loaded and registered no modules has no
matching children, and counting by them would make the palette claim the plugin is not there.

## Script languages (`script_language.hpp`, `languages/`, `script_modules.hpp`)

The studio can not only load script modules but create them, and that ability is parameterised by one
value, `script_language`. The type did not appear out of a love of generality: supporting Python turned
out to be **two different things under one name**. On one side, conventions any bridge has: the bridge's
file name, the scripts subdirectory, the package name, the extension, the search variable. On the
other, rules belonging to CPython specifically. While there was one language, separating them was
pointless; with a second, leaving them unseparated would mean a second copy of six hundred lines that
diverges from the first on the very first edit.

**What belongs to the language rather than to the studio.** The difference between Python and Lua is
exhausted by one field — `missing_dependency_hint`. For Lua it is empty: the bridge carries the
interpreter inside its file, it has no absent runtime, and a hint with nothing to say is worse than no
hint.

**A field for "one bridge per process" is absent here, and that is a trap already worked through.** The
temptation to add one goes like this: surplus copies of the bridge are withdrawn because the Python
bridge's `Ctx` lives in a static of its own DLL and the copies that lost the inittab race refuse to
create modules — while Lua has an interpreter per instance and there is nobody to take anything from.
That is half the reason. The other half does not depend on the language: the host has one registry and
the scan directories are derived from one list of search directories, so two copies of **any** bridge
walk the same scripts and register the same names; `module_registrar::add` refuses the duplicate,
`module_loader` withdraws the whole file, and a permanently red `failed` row is left in the dock.
Reproducible with two folders: create a module in one, then in the other. So `keep_one_bridge` works for
every language. CPython's reason is the stronger one — but a stronger reason for the same rule is not a
different rule.

**The name checks differ between languages, and that is not an oversight.** In Python a name has to
survive being turned into a class name, so `_1` is forbidden — the derived class would be called `1`. In
Lua there is no derived symbol at all and `_1` is legal. What they share is the ban on `atp`, but by
different mechanisms: in Python `atp.py` beside the package hijacks `sys.modules["atp"]`, and in Lua
`atp.lua` *is* the package file.

**A package is not always a directory.** In Python it is `python/atp/`, in Lua the single file
`lua/atp.lua`. Hence `package_is_directory`, a branch in `provision_folder`, and the fact that freshness
is measured over the language's own sources (`file_extension`) rather than over any file: a cache the
runtime writes beside them on the first import would otherwise make a stale copy look newer than the
platform's, and it would never be updated.

**A module folder may be bilingual.** The languages differ in every path they touch, so provisioning
one folder for a second language breaks nothing, and the folder itself remains **one** plugin search
directory. That is exactly why "the last folder" is looked up per language: the one that has a `python/`
is the right suggestion for Python and the wrong one for Lua.

**`script_environment` is one object, not a value per language.** The inherited tail of every variable
is captured once, at startup, before a second thread exists: a later read would already see what the
studio wrote itself. And every place that changes the search directories is obliged to update **all**
the languages rather than the one the author of the edit was thinking about — a forgotten language shows
up as modules silently gone from the palette, with not a line anywhere.

The dialog is a single `File → New module…` menu item with the language as its first field, rather than
two items side by side: everything else in the gesture is the same across languages, and the note at the
bottom has to change with the choice anyway, so two items would be two copies of one dialog. The chosen
language is remembered in the profile (`last_script_language`), and a value naming a language this build
does not have is resolved by the dialog itself, so that a profile from another build opens rather than
being rejected.

## Attached mode: a mirror of somebody else's pipeline

- **Why.** `atp_app --control <port>` exposes everything needed for observation, but the only tool able
  to **show** the graph worked with its own in-process run alone. `Host > Attach…` connects the window
  to a running host; `Detach` brings the deferred project back.
- **The mirror is an ordinary `project`.** The description from `describe_pipeline` is folded into a
  document of the current schema, run through the **real** `runtime::validate` and opened as a project
  (`studio/project_from_description.hpp`). Two benefits follow: the canvas, the tree and the inspector
  work with no change at all, and if somebody else's pipeline does not fold into a valid config we learn
  about it at once and with a text, rather than after drawing half the graph. Node positions come from
  `auto_layout` — a mirror has nowhere to take them from.
- **`runtime_view_base` rather than `session` directly.** The panels read the runtime through an
  interface with two implementations (`local_runtime`, `remote_runtime`). **`live_root()` is not part of
  it**: a `group*` means nothing over a socket, and it is precisely what the inspector would reach live
  values through. Instead of a pointer to a tree there is `live_properties(path)` — a question instead of
  access; that is also what separates the mixed sources in the grid (the schema from the catalog, the
  values from the tree). `error_text()` is a string, because a remote error arrives as text and
  reconstructing an exception from it would be invention. Virtual functions here rather than a concept as
  on the server side: there are two implementations and the choice is made at run time.
- **The client is synchronous, with a timeout** (500 ms for a poll, 2 s for a user action). An
  asynchronous client with its own thread and a queue back into the GUI thread would be a second
  concurrency model for the sake of a 4 Hz poll; the synchronous variant's worst case is one timeout wait
  and a transition to the detached state.
- **What is cached and why.** The description is read once: somebody else's host does not rearrange the
  graph under an observer, and rebuilding the project on every tick would throw away the selection and
  the positions. It is re-read on `Refresh the mirror` and after a property edit — so that a value the
  module normalised comes back as the module holds it rather than as it was typed. The status is cached
  for 100 ms: the panels ask `running()` dozens of times per repaint, and each question would otherwise
  cost a round trip.
- **A broken connection means an automatic detach**, with a message in the log. Showing yesterday's
  numbers as today's is the worst possible answer from an observation tool.
- **Shutting the remote host down is a menu item with a confirmation** naming the address, rather than a
  button two pixels away from the local Stop: one word, a different scope of action. `Save` is disabled
  in this mode (a mirror has no file of its own, and the deferred project's path belongs to something
  else), while `Save As…` is allowed — a mirror is a valid config, and the log says outright what the
  export lacks.
- **What a mirror does not carry, and why:** plugins, the thread layout and the assignments. The runtime
  stores none of that in readable form — a `group::connection` is a pair of ports and nothing else, and
  the layout is known to `pipeline_runner` rather than to `group`. The real threads with their counters
  are visible in the Runtime dock from `get_status`, and `detached` is visible on the children.
- **Values arrive as strings.** A property travels as a name, a kind, a default, a set of options and a
  current value — all as text, canonical for its codec — because a C++ type's name is how somebody else's
  compiler writes it, and matching that between two toolchains would be guesswork. The kind is named by a
  word, and `remote_runtime::kind_of` tolerates the older `"number"`. A mirror carries no values on the
  connections, for the same reason the local mode does not show them: an output keeps no cache.
