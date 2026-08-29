# The module config and the document model

The third declared entity beside ports and properties — a structural setting arriving in the
constructor — and the type the whole platform document is described with. It runs through every
subsystem at once, which is why it is separated from all of them. The document map and the tree layout
are in `../architecture.md`.

## The module config (`module/module_config.hpp`, `runtime/config_binding.hpp`, schema 1.0, `plugin_abi` 1)

### The declared form: `atp::module_config`

A module's config is **a declaration with values in it**: an heir of `atp::module_config` names its
fields as reference members exactly as an io section declares ports, while the module names its type
through `using config_type = ...` and takes ownership of a ready object in its constructor, as a
`std::unique_ptr<my_config>`.

Three primitives: `field` (a scalar), `group` (a nested object), `list` (an array of scalars or
groups). Required is expressed by the **absence of a default**: `field<double>("gain")` is required,
`field("gain", 1.0)` is not; there is deliberately no separate word for it, since two names for one
notion drift apart. A separate overload from `const char*` exists because a string literal is not one
of the four scalar forms — the same reason `config::node` has one.

Decisions with a price behind them rather than a preference:

- **The base knows nothing about the document at all.** No node, no parser, no path grammar: the
  object is a declaration with values, and filling it from a document is the host's job through
  `entries()`. That is the whole point of the split: the type a module author names in their own
  source does not drag a document library across the plugin ABI, and the host can describe and edit
  the config of a module it never built.
- **One representation rather than three.** The schema, the values and the rules for what is worth
  writing are one object: the description of a config **is** the config, `module_config::entry`
  answers everything a separate description would be asked, and the writing rules live in the single
  `runtime::save_fields`. Kept apart they would be three copies of one piece of knowledge — the
  declaration, the description of the declaration for hosts, and the studio's writing rules — diverging
  on the first edit.
- **The host fills and checks, not the object.** An unknown key is a key no declaration claimed, and
  that can only be known once all of them are declared — that is, after the heir's last member
  initializer, where with `using module_config::module_config;` there is not a line of code. So
  `runtime::load_fields` (`runtime/config_binding.hpp`) walks the declared fields and the document's
  keys from both sides, **never throws**, and returns a list of problems as "path: what is wrong"
  lines; `load_fields_or_throw` throws, naming the file and every problem at once. The object is built
  **once**: the factory hands the config out rather than taking it, so there is no double-construction
  scheme here ("validate, then build again inside the module").
- **An integer read into a `double` widens; never the reverse.** `config::node` keeps integers and
  reals apart, and JSON writes `48000` rather than `48000.0`. Without the widening a real field would
  **silently** take its default — the worst kind of error. The reverse direction is refused even with
  no fractional part: `2.0` and `2` are written differently in the document, and
  `encode(decode(doc)) == doc` tells them apart.
- **The storage is a `deque`, and so is a list.** The heir holds references into the base, which makes
  `module_config` neither copyable nor movable, and a `std::vector` rehomes its elements as it grows.
  For the same reason `list<T>` returns a `std::deque<T>&`: a `std::vector<T>` of a non-movable type
  simply would not compile. And by the same reason again, `entry::resize` grows an array of objects
  through `emplace_back` rather than `deque::resize`, which requires MoveInsertable.
- **`is_set()` is part of the contract, not decoration.** Without it a required field nobody filled is
  indistinguishable from one filled with the zero of its type, and a host saving the config back would
  write `0` for it. The flag is raised by a write **through an `entry`**, not through the bound
  reference: the loader and the editor write through `entry`, whereas a module's own writes into its
  own config are its business and not the document's.
- **The schema is readable without a module** — that is what `module_factory_base::make_config()` is,
  handing out the object at its declared defaults. An empty answer (`nullptr`) means the config does
  not reach the module at all; an object with no fields means it does but the module does not describe
  it — which is how a module reads its own file through `text()` having declared
  `using config_type = atp::module_config;`, and how the C path and both bridges are built. Those are
  different answers, and MCP prints the `"config"` key on the first of them and an empty `"fields"` on
  the second; treating a field-less object as "takes no settings" would tell a model that writing
  `"config": "file:rig.ini"` for this module is pointless. In `studio::module_info` this is
  `takes_config` beside `config_schema`, as a separate field, because the palette does not store a
  `raw_config` prototype at all. `module_declaration` carries no schema.
- **An enumeration is not a seventh kind of field.** `field("layout", channel_layout::stereo)` binds a
  `channel_layout&`, the field's kind is `string`, and what makes it an enumeration is a non-empty
  `entry::options()` — exactly the rule properties live by (`property_codec<TEnum>::kind == text`),
  and it exists so that a module's config and its properties do not say one thing in two ways. Outward
  that is a single name: a string in the document, a string on the C path and in both bridges, and the
  `"enum"` key in MCP, the same one a property uses. The set comes either from the type's name table or
  from `io::allowed(...)` listed at the declaration, and the second **replaces** the first — which is
  how a module narrows an enum to the subset it supports. The invariant that the value is always inside
  the set is held by three checks against one canonical text: the default at declaration, a typed write,
  and `from_string`. That is why `to_string()` never throws and the whole string layer is safe for
  free.

  Three consequences worth knowing before editing this. An enum field's storage is a slot in `owned_`
  rather than one of the four deques: the set of enum types is **open**, which is also the answer to
  "why not a single deque of a variant" — a variant has to enumerate every form, and that list does not
  end. `entry` keeps a `std::type_index` beside the kind, because two different enums and a plain
  string all answer the same `field_kind::string`, and casting one storage to another would
  reinterpret an object of a different size. And `to_string`/`from_string` moved into a table of
  function pointers taken from `property_codec<T>` at declaration time, for the same reason: the kind
  does not determine which codec prints the value. A `switch` disappeared from both functions as a side
  effect, and `from_string` became the **single door** for the whole string kind, host side included:
  `load_fields` reading an enum through `set(std::string)` would throw where it is obliged to return a
  problem line.

- **The whole document tree is `atp::runtime::raw_config`**, an heir of the same base living on the
  host's side. It declares no fields and instead takes the document as it is (`adopt`), offering it
  through `root()`/`find(path)`/`at(path)`. The C path and both bridges use it: on that side there is
  no C++ type to bind references into. Imperative traversal of the tree is **not available to a C++
  module** — whoever reads their own `.ini` declares `using config_type = atp::module_config;` and
  takes `text()`.
- **The studio edits the object.** `config_tree` takes its own instance from the factory, fills it
  with `load_fields`, builds rows by walking `entry`, and writes back with `save_fields`. The rule
  "what equals the default is not written" is shared by properties and configs for one reason:
  otherwise the document grows to the full schema from merely opening a module, and a diff shows edits
  nobody made.
- **A value of the wrong shape cancels the rows entirely.** The `load_fields`/`save_fields` pair has a
  consequence neither of them has alone: a field the loader could not read stays **unset**, and
  `save_fields` does not write what is unset — so the value disappears from the document on the first
  edit to any neighbouring field. `carry_unknown` does not help here: it rescues **undeclared** keys
  only. So the studio has `config_misfits`, which walks the declaration against the document and names
  every field of the wrong shape with the same line `load_fields` would have given; when the answer is
  non-empty the rows are not drawn at all, and in their place stands a text editor with those lines
  above it, out of which the document is repaired — the form comes back by itself once it fits again.
  The key point is that this is **not** the question "did `load_fields` complain": a required unfilled
  field and an undeclared key are complaints too, but both survive a save (the first as an empty cell,
  the second through `carry_unknown`), and so they do not cancel the form. Locking it on any complaint
  would mean never showing the form to a module that has even one required field: an empty config is
  "required and absent" from the very start.

### Transport: `module_config`

The third declared entity beside ports and properties, and it exists for one set of requirements that
no existing channel covers as a whole: the value is **not a scalar** (a list, a table, a nested
object), it is needed **before `initialize`**, it is **not edited live**, it should not occupy a row in
the property grid, and it wants to be **shared between modules**.

- **Why the constructor rather than `initialize`.** The constructor is the only point earlier than
  both `connect` and `initialize`, and it is where the `make<>()` calls that declare ports happen.
  Ports from a config are out of scope, but the delivery point chosen leaves the door open for them in
  the strongest sense: a C++ module will be able to declare ports from its config with no change to the
  platform. The C path will stay static, its descriptor being per registration.
- **Why a property does not fit.** A property is a scalar with a codec to text and back; it is visible
  in the inspector and edited live. That is exactly a violation of requirements (1) and (3).
- **Why the config's type and the node's type are different types.** What arrives in the constructor is
  the config **as a whole** — the declared fields with their values and, if it came from a file, that
  file's bytes and its name (`atp::module_config`, `include/atp/module/module_config.hpp`).
  `atp::config::node` describes **one node** of a document tree and stays exactly that. Merging them
  would mean giving a node the properties of a whole document: `text()` on a nested node is
  meaningless, and `find("a.b")` on a node would have to be explained relative to what.
- **Why the exception is called `access_error`.** `atp::config::access_error` and
  `atp::runtime::config_error` are different failures at different layers: the first means one node or
  one path is wrong, the second that the host could not read, validate or build a config. While the
  first was simply `error`, only the namespaces told them apart, and a `catch` written from memory
  caught the wrong one.
- **Why a type of our own rather than `nlohmann::json`.** The signature of `create()` is part of the
  plugin ABI, and naming `nlohmann::json` in it means dragging the library into every plugin — exactly
  what `atp_runtime` is not exported for. So `atp::config::node`
  (`include/atp/config/node.hpp`) is a closed variant of seven forms with no parser and no `dump()`,
  and the conversion from `nlohmann::json` to `config::node` lives on the host's side in
  `runtime/config_value_json.hpp`. The one conversion failure is a `uint64_t` above `int64_t`'s max: a
  form the closed set does not have, and a silent overflow would hand the module a different number.
- **Why integer is separated from real.** The same argument by which the model stores values as JSON
  nodes rather than strings and `5` does not merge with `"5"`: a config that says `3` is a counter, not
  a `3.0`. The set of port types already distinguishes `i64` from `f64`.
- **Why an object is ordered — and what that does NOT mean.** An object is stored as a vector of pairs
  rather than a map, for reproducibility of traversal: the same input gives the same handles of the C
  path's flat index and the same insertion order into a bridge's dictionary. It does **not** give the
  order **from the file**, and that cannot be promised: the document is read as `nlohmann::json`, that
  is, on a `std::map`, and the keys are sorted by the time parsing is done — `encode`'s docblock admits
  the same about `expose` aliases. For the author's order to survive, the whole document would have to
  be read as `ordered_json`, and that means the signatures of `validate`, `decode`, `encode`, the MCP
  tools and the project all at once; deliberately not done.
- **Path access and two different failures.** The path grammar lives **in the runtime**, on
  `atp::runtime::raw_config` (`runtime/raw_config.hpp`), because it is needed only by those who take
  the document whole — the C path and the bridges. `raw_config::find("channels[2].rate")`: segments
  split on a dot, an index as `[N]` after a segment, a leading `[N]` for an array at the root. A key
  containing `.` or `[` is unreachable by path and is obtained through `root()`; escaping is
  deliberately not introduced. There are exactly two failures and they differ in nature: **a malformed
  path throws `config::access_error`** — including from `find`, which is therefore not `noexcept` —
  because the path was written by the module's author in their own source, and a silent `nullptr` on a
  typo turns it into an hour of debugging; whereas **something absent** answers `nullptr` from `find`
  and throws from `at`, naming the full path and where it stopped. Parsing always runs to the end of
  the string and a failed lookup is merely remembered: otherwise one and the same malformed path would
  throw against one set of data and answer "not there" against another. The full path in the message is
  possible not because the value learned its parent — that was rejected, as it would require owning
  references inside the value type — but because the caller passed it. There are no typed reads on the
  class: a read without a fallback is `config::node`'s own vocabulary
  (`cfg.at("audio.rate").as_int()`), and one with a fallback is the free utilities
  `int_or`/`bool_or`/`double_or`/`string_or`, which take a **nullable** node, so one utility serves both
  levels of lookup. They are the only ones: `node::*_at(key, fallback)` overloads existed alongside for
  a while, covered only one of the two levels — a second vocabulary for the same operation — and were
  removed. The throwing `*_at(key)` remain on the node, for a message that names both the key and the
  two forms.
- **Why a string is always a reference.** A module's node writes either an object in place or a
  string, and the string is never a literal: the unambiguity of the grammar depends on it. A config
  that is itself a string is written as `{"value": "…"}`. Parsing splits on the **first** colon, the
  same way `parse_property_override` splits "path.prop=value" on the first `=`: no prefix means the
  default, that is, an entry of the top-level `configs`; any prefix is currently a validator error
  naming the prefix, while a new source later costs one `case` and no schema change (`file:` and
  `literal:` were discussed and deferred). An entry's name must therefore contain no colon.
- **Why an entry of `configs` must be an object.** The restriction on the root looks superfluous —
  there is no ambiguity with the `"config"` key there — but without it the guarantee the constructor
  relies on would depend on spelling: an inline config must be an object, while the same config through
  a reference could arrive as an array or a scalar, and a module would be writing code about which
  syntax the file's author chose. One rule is cheaper: **a config's root is always an object or
  `null`**. An entry's contents are checked by nothing, which is the point of it.
- **An absent config and an empty object are distinguishable.** With the key absent the module gets
  the `null` form, and that is not the same as `{}`: a module is entitled to rely on the difference.
- **Where a reference is resolved.** In the builder (`pipeline_builder::detail::resolve_config`), not
  in the model. The model stores what was written **verbatim** — a string stays a string, an object an
  object — and then `encode` is a copy of the node and the invariant `encode(decode(doc)) == doc` holds
  with no separate logic. Expanding a reference on save is not allowed, and verbatim storage is the
  cheapest way of not doing it. The validator catches both a dangling name and an unknown prefix ahead
  of the builder, but the builder is also called from the studio, bypassing document validation, so
  the same two checks exist there as well.
- **The `file:` source.** The string `"file:<path>"` attaches a file, and it is permitted in two
  places: on a module, and as an entry of `configs`, where one file then serves several modules. Such
  an entry **cannot be a bare reference** to another entry — and that is the whole defence against
  cycles: a chain of references is exactly one step long, so no set of visited nodes and no depth
  counter is needed anywhere. The rule is checked both by the validator and by
  `project::set_shared_config`, that is, in both places a block is ever written.
- **The extension decides the format.** A `.json` must parse: broken JSON is a host error with a
  position rather than a quiet slide into "unknown format", which would hand the module an empty config
  instead of an error. Any other extension, and the absence of one, means **opaque** text: a `null`
  root, `is_opaque()` true, the file's bytes in `text()` and the path in `origin()`. The host learns no
  new formats, and a module that speaks YAML or INI does not wait for it to. `is_opaque()` is not
  derivable from the rest: a `.json` whose content is `null` also leaves an empty tree beside a
  non-empty text.
- **What a path is resolved against.** The directory of the document the string is written in — the
  same rule plugin paths follow. An empty directory plus a relative path is an error naming exactly
  that ("needs the document's directory") rather than "file not found": the difference between "the
  project is unsaved" and "a typo in the name" is worth one check. So the studio passes `saved_dir()`,
  empty for an unsaved project, rather than `config_dir()` with its substitution of the process's
  current directory.
- **How `file:` is not a duplicate of `$include`.** `$include` is expanded in `config_loader` before
  validation, so the model receives the contents: saving the project **inlines** the file and the
  reference disappears. `file:` is stored verbatim and remains a reference after any number of saves,
  can express an opaque format `$include` cannot handle at all, and leaves the module an `origin()`.
  `$include` assembles a **document**, `file:` attaches a **module's config**; both mechanisms stay.
- **The validator's error cascade.** If the `"configs"` block itself is not an object, the set of known
  names is empty and the reference check is skipped: otherwise every reference in the document would
  collect a second error about a dangling name on top of the first about a broken block. A cause is to
  be named once.

### Declaring a config on the C path (`atp_config_field_desc`, `dynamic_config`, `c_config`)

A module beyond the C boundary also **declares** its config rather than merely reading it. The field
descriptor was appended to `atp_module_desc` as two members behind `struct_size` (`config_fields`,
`config_field_count`), **with no `ATP_C_ABI` bump**, and a plugin that declares nothing behaves exactly
as before — it gets a `raw_config` and is edited as text.

`atp_config_field_desc` **carries no `struct_size`**, unlike `atp_module_desc`, and that is a decision
rather than an omission: it is read as an **array**, and a growing element type in an array breaks the
stride — the host would be indexing by its own `sizeof` over the plugin's shorter elements.
`atp_input_desc`, `atp_output_desc` and `atp_property_desc` are frozen for exactly that reason. Only
the module descriptor, taken by pointer one at a time, may grow.

An enumeration is `ATP_FIELD_STRING` plus a non-empty `options`, not a seventh kind; required is
`default_value == NULL`. Both wordings match what `atp_property_desc` and `module_config::entry`
already say, so the vocabulary is one on both sides of the boundary.

The host side is two classes and one function.

- **`atp::dynamic_config`** (`module/dynamic_config.hpp`, umbrella `hosting.hpp`) is a config whose
  shape is data rather than a type. All four declarers in the base take the shape from `T`, and
  `list<T>` does so **inside a captureless lambda**, so an element described by data cannot be built
  that way at all. Hence the base gained **one** protected name, `declare(dynamic_field)`, rather than
  four: extending `protected` for the sake of one consumer is expensive, and a `friend` would make an
  SDK header addressed to a module author name a class from the host's audience — a dependency in the
  wrong direction. `entry` did not change: an array of objects is stored in `element_array` inside the
  existing `owned_`, both string halves of `list_ops` stay `nullptr`, and that is the same "array of
  objects" discriminator `entry` already distinguishes kinds by. No data was added and no virtual
  functions either — **`plugin_abi` did not move**.
- **`runtime::c_config`** is an heir of `dynamic_config` that builds fields from descriptors
  recursively. The tree the plugin reads is built from **its own filled fields** rather than from the
  document, and that is the whole difference: the module sees every declared key in place, defaults
  included.
- **`values_of`** (`runtime/config_binding.hpp`) is the deliberate opposite of `save_fields`. The
  latter thins out: it drops an unwritten field and one that returned to its default, because a saved
  project should carry no noise. A module needs the reverse: a declared and unwritten key is a key at
  its default, not an absent one. **They must not be merged** — merging would wash the defaults out of
  every module's config.

`c_module` holds its config as a `module_config` and reaches the tree through the data-less mixin
`runtime::config_tree_source`. There can be no common base here: `raw_config` inherits `module_config`
directly while `c_config` does so through `dynamic_config`, and a common ancestor would mean inheriting
it twice. The path grammar accordingly moved out of `raw_config` into the free functions of
`runtime/config_path.hpp` (`find_path`, `at_path`): it was always about a node and a path rather than
about who owns them, and both configs need the same one.

Nothing changed downstream. The `dynamic_cast<const raw_config*>` check in `module_manager::describe`
stayed correct by itself — a `c_config` is not a `raw_config`, so its prototype is kept as
`config_schema` and the inspector draws a tree. `load_fields`, `save_fields`, `config_misfits`,
`config_tree` and MCP's `"config"` key all walk `entries()` and do not ask who declared them.

### The C path

Eleven callbacks appended **to the end** of `atp_api` behind `struct_size` — and **with no `ATP_C_ABI`
bump**: `plugin_c.h` declares adding callbacks the regular way of growing. A node is addressed by an
opaque `uint32_t`; zero is reserved for `ATP_CONFIG_NONE`, and the root cannot be zero, so an author's
result check is the same one for `config_find` and `config_child_at`. The host keeps a flat DFS
preorder index, assembled once before `desc.create` — the config must be readable inside it already,
that being the C analogue of a constructor.

The rejected alternative was handing the config over as one JSON string: Lua has no built-in parser, so
one would have to be vendored into the package, which costs more than seven pointers.

Two things worth knowing before touching this.

- **A config's text lives for the module's whole lifetime**, not until the next read, and that is
  deliberately stronger than the ports' contract. A port's text is transient because it is a copy taken
  into a shared scratch buffer, whereas a config's string already lies in the `config::node` tree the
  module holds from its constructor to its destructor. The pointer is handed straight into the tree
  (`config::node::string_ptr`): cheaper, safer, and without the asymmetry in which `config_key_at`
  would be eternal while a value died from an unrelated `get_input` on the same `ctx`.
  `config_value_of` is meaningful only for the four scalar forms — `atp_kind` has neither null nor a
  container — and on `null`, an array or an object it refuses without touching `*out`.
- **The host's `guarded` will not do for these callbacks.** It is boolean by construction, doing
  `return body(self) ? 1 : 0`, because all eight of the pre-config callbacks answer with success or
  refusal. Four of the new ones return a handle and `config_kind` returns a form number, and through a
  boolean guard `ATP_CONFIG_OBJECT` would become `1`, that is `ATP_CONFIG_BOOL`, and any handle would
  become the root; for the four that is a compile error, for `config_kind` a silent lie. So
  `guarded_value<TRet>` lives beside it with a **passed-in** failure value (`ATP_CONFIG_NONE` for
  handles, `ATP_CONFIG_NULL` for the form), and the boolean one stays untouched.

Looking a handle up by pointer is a linear pass over the index. Chosen deliberately: the index is
preorder, the children's handles are computable and `O(1)` is reachable, but a config is tens of nodes
read once in a constructor, and an extra structure costs more than the pass.

The last four callbacks (`config_find_path`, `config_text`, `config_origin`, `config_is_opaque`)
arrived after the other seven and are asked for by **their own** detector, `atp_api_has_config_text`: a
host carrying the seven and not the four answers yes to `atp_api_has_config` and must answer no here,
or a plugin would read four null pointers. Two consequences for those four:

- **A malformed path returns `ATP_CONFIG_NONE` but is not lost.** There is nothing to throw across the
  boundary, so the adapter implements `config_find_path` through `guarded_value`, and a
  `config::access_error` lands in the same "a host error inside a callback is stored and rethrown"
  channel the others use. For that, `c_module`'s constructor **calls `rethrow_pending()` after
  `desc.create`**: without it an error stored there would live until the first `initialize` and be
  presented as an initialization failure. The instance the plugin already built has to be destroyed
  before the throw — a throw from a constructor runs no destructor, and nothing else would ever call
  `desc.destroy`.
- **A path is followed only from the root.** The flat index addresses nodes by number, and walking
  from an arbitrary node would require the reverse mapping; for any other node the callback answers
  `ATP_CONFIG_NONE`, and that is written into `plugin_c.h` as a limitation rather than left as a
  surprise.

### The bridges

Each bridge walks the tree **once, when an instance is created**, and materialises a native value: a
`dict`/`list` in Python, a table in Lua. A script's author sees an ordinary dictionary rather than
handles and paths.

In Python the config is bound **before `__init__`** (`_create` builds the object through `__new__`,
sets `config`, and only then calls the constructor) — precisely because this is a channel arriving
earlier than `initialize`: a constructor must be entitled to read `self.config`. Lua has no
constructor, and the `config` field is put into the instance at `atp._instantiate`.

Three fields travel beside the tree — `config_text`, `config_origin`, `config_opaque` — and are bound
in the same place at the same time: before `__init__` in Python, at `atp._instantiate` in Lua. Here the
bridges **deliberately diverge**: Python decodes the text as UTF-8 strictly, so a file in another
encoding fails the module's creation with the file's name and a byte position, while Lua hands the
bytes over as they are, a Lua string being bytes. Neither produces silent mojibake, and that is all
that was required.

One honest loss: **a Lua table does not preserve key order**, whereas `config::node` and Python's
`dict` keep the order a value arrived in. The `__newindex` proxy the Lua package uses to hold
declaration order does not extend here: in a config, order addresses nothing. And remember it is not
the order from the file (see above).

### The studio

The project carries a config through saving, opening and **undo/redo** by itself, with no change: it
holds a `runtime::config` and does everything through `runtime::encode`/`decode`, and verbatim storage
makes the round trip free. MCP's vocabulary is untouched too and there is nothing to touch there —
`get_document` hands back `runtime::encode(project().config())` whole.

A config is edited in **a separate JSON area** in the inspector rather than as a row in the property
grid: the requirement "must not occupy a row in the grid" is met by itself, because a config is not a
property. The text is parsed on focus loss, and an edit reaches the project only if it parsed and
actually differs — an unfinished object must neither enter the document nor push a snapshot onto the
undo stack.

**While the pipeline runs, a config is read-only**, unlike properties, which stay live. This is not a
new rule but a consequence of an existing one: a config reaches the constructor, therefore it belongs
to the project's structure, and the structure is read-only while running. It is checked where every
other structural edit is, in the window through `state_.view->running()`; `project` is a document and
knows nothing about the runner, and the inspector's `apply_lock` already disables everything but the
property block, so no new machinery was needed.

**A config attached by file is shown read-only by the inspector.** The string `"file:…"` is not a
block name (a name cannot contain a colon at all), so it is held in a separate field, shown as written
in the "source" row, and beneath it stands the file's contents — or the reason it cannot be read. The
preview is made by the same `runtime::load_config_source` call the run will use, so an unreadable file
says here exactly what it will say then. The studio neither edits nor rewrites the file itself: it
belongs to somebody else, and the platform may not know its format. Focus-out in this state writes
nothing — otherwise the preview would replace the reference with its own text. The decision is made
**after dereferencing the reference** rather than by what is written in the module's node: a shared
block may itself be a `"file:…"` string, and a module naming such a block would otherwise get an
editable field with the text `"file:rig.ini"` inside it, where one commit would replace the reference
with an object, and for everyone referring to that block at once.

`project::set_shared_config`/`clear_shared_config` exist for more than symmetry: without them the
reference form would be unreachable from the studio — the project could only spell a config out in
place — and the block a saved reference points at has to appear, or the document will not pass
validation when opened. Deleting a block does not rewrite the modules referring to it: that would be a
second, silent edit, whereas the validator will name a dangling reference plainly at the next check.

From the inspector a block is **not deleted at all**: an empty editor with a block selected clears its
own module's config and leaves the block declared — exactly as returning the source to inline does.
Deletion is a document-level edit made from one module's panel, and it would leave every other
referring module with a reference into nothing; detaching speaks only about its own module and stays
silent about the rest. The price is known and accepted: `clear_shared_config` remains a model operation
with no caller in the GUI, so a dead block is removed by editing the project file.

## The document model: why it is ours rather than a library's (`config/node.hpp`, `runtime/json_codec.hpp`)

JSON here is **a format at two boundaries** and nowhere else: the config file on disk and the MCP
wire. In the middle — what is validated, decoded, edited in the studio and pushed onto the undo stack —
is `atp::config::node`, a closed variant of seven forms, declared in the SDK for the sake of the plugin
boundary. A library in the middle would be not a format but a **domain type**, and replacing it would
mean rewriting the config subsystem, the studio's project, its undo and the inspector all at once.

The decoupling is done like this. `runtime/json_codec.hpp` declares `json_parse` / `try_json_parse` /
`json_dump` over `config::node` and **names no library at all** — which is the only reason it has a
`.cpp`: the header is part of the `<atp/runtime.hpp>` umbrella, and an inline body would bring the
library back into every translation unit that only wanted a pipeline. `src/runtime/json_codec.cpp` is
the runtime's only file naming `nlohmann`, and it is built into a separate STATIC target, `atp_json`,
where the library is **PRIVATE**. So `atp_runtime` hands it out neither through a header nor through an
include path, and the targets that genuinely speak the protocol (`atp_mcp_lib`, `atp_studio_lib`) ask
for it by name. Changing the library means rewriting one `.cpp`; and the same target is the template
if the rest of the runtime moves out of headers into `.cpp` files.

`config::node` carries equality and in-place editing (an inserting `operator[]` by key, `push_back`
and `erase`). That is not a weakening of the "no parser, no `dump()`" rule: the rule is that the type
**must not name a document library**, because `create()` is part of the plugin ABI, not that the type
is read-only; no data was added to the objects by in-place editing, so `plugin_abi` did not move.
There is no separate `set(key, value)` — it would be a literal duplicate of `node[key] = value`;
`erase` exists and is called by `config_tree::remove_element`, deleting a list element in the
inspector.

Walking an object and an array is `entries()` and `elements()`, each handing back the vector itself
(and, for a form it does not belong to, a shared empty one, so that a node can be walked without asking
its kind first, as with `size()`). Without them every walk in the tree is an index loop over `key_at(i)`
and `operator[](i)`, and that is the main price of a document type of our own. The array accessor is
named `elements()` rather than `items()` because `nlohmann::json::items()` means exactly the opposite —
an object's key/value pairs — and both types travel side by side here: in
`runtime/config_value_json.hpp` each of the two appears once. A false friend that compiles and silently
walks nothing costs more than a long name.

One consequence is worth knowing, because it is not obvious. A `config::node` object preserves the
order it was filled in, and its equality is **order-sensitive**, whereas `encode` writes keys in the
order its own code runs. So two documents identical in meaning may differ as trees. The round-trip
invariant is therefore pinned on the **text**: `json_dump(encode(decode(doc))) == json_dump(doc)`.
`json_dump` sorts keys, and that is no accident: a saved document must be reproducible byte for byte
regardless of the order it was filled in. A check on the tree would be both weaker and falsely red.
