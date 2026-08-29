# The module author's SDK (`include/atp/`)

What a module author writes against, and the contract under which their module is loaded: the io
layer, the module layer, the plugin ABI and its C variant. The bridges that stand on the C path are in
`bridges.md`; the module config as a channel is in `config.md`. The document map and the tree layout
are in `../architecture.md`.

## The io layer (`include/atp/io/`)

The hierarchy: `io_base` → `input_base`/`output_base` → `input<T>` → `queued_input<T>`; `output<T>`.

- **`io_base`**: a name, `typeid(T)` as a `type_index` (the source of truth about the type), and a
  mutex. Thread safety is a **property of the instance**, chosen with the `safe`/`unsafe` tag at
  construction; `unsafe` pays only for a branch (a deferred lock). `thread_safe()` is what the runner
  reads during validation.
- **Delivery without dynamic_cast** (AUTOSAR A5-2-1): the input itself answers `accepts(type_index)`
  — checked once, at connect — and receives through `deliver(const void*, erased_type)`. `deliver` is
  NVI: reception happens in the heir's private `do_deliver`, and afterwards the base calls
  `notifier_base::notify()` if the executor attached one with `set_notifier`. `notify` is a platform
  signal for waking a consumer thread, not a callback carrying data: the "no push callbacks" contract
  is intact, and a direct write into an input (`operator()`) does not signal at all. `input<std::any>`
  accepts everything (boxed through `erased_of<T>().box`); the reverse (`output<std::any>` into a typed
  input) is deliberately unsupported. The `erased_of<T>` statics are duplicated per DLL — compare their
  contents only, never their addresses.
- **Type identity across a library boundary** (`atp/support/type_compare.hpp`): the `type_index`
  values being compared are born in different binaries (the output's tag where the output is
  instantiated, the input's where the input is), and `type_info::operator==` is
  implementation-defined: libstdc++ and MSVC's STL compare the name, Apple's libc++ compares the
  address alone, assuming typeinfo is coalesced by the dynamic loader. Plugins are built with hidden
  visibility, which is exactly what forbids that coalescing. So all four comparison points that cross
  the boundary go through `atp::same_type` (by name, falling back to address for internal-linkage
  types): `accepts`, `io_registry::get`, the `service_directory` key (via `type_name_less`) and the
  studio's `types_compatible`. Port and interface types are therefore required to have external
  linkage.
- **Pull-model reading**: `get()` is a state input (a copy, read as often as you like), `take()` is an
  event input (removed exactly once; for `queued_input`, the head of the FIFO). There are **no** push
  callbacks by design: on the writer's thread, delivery does nothing but `store()` under the input's
  lock, and no foreign code runs there. The "callback on a value" ergonomics come from **`watcher`**
  instead: rules of the form "input → handler" (and "property changed → handler") are registered in
  `initialize` and `poll()` is called from `iterate`, so handlers run on the module's own thread and
  races are excluded by construction.
- **`output<T>`**: push delivery to connected inputs, **with no cache**. Delivery happens outside the
  output's lock — each input takes its own — so there are no nested locks. An output holds raw
  pointers: `disconnect()` before destroying an input is the caller's contract. The only observation
  for tooling is `write_count()`, the write generation, from which a tool highlights connection
  activity by the change between polls. Only safe instances are observable; an unsafe one answers 0
  ("not observable"), since reading it from outside would be a race. A late subscriber receives
  nothing until the next write.
- **Why there is no cache.** Caching the last written value would cost a copy of the payload **on
  every write** — a quarter of the cost of a 256 KiB write (measured) — and everyone would pay it,
  including headless runs with not one reader. There is nothing to buy with that: nobody outside the
  tests calls `get()` on an output, and `connect(in, replay)` would never fire in a real host, because
  `build_group` makes every connection **before** `runner.start()`, when nobody has written yet. Hence
  also the fact that tooling shows connection activity rather than a value: the display cannot be moved
  to the input's side, because `take()` removes the value and in a healthy pipeline there is almost
  never anything there.
- **The write path: one copy per subscriber.** The consistent snapshot every subscriber sees is the
  caller's own object, alive for the whole call, so writing a value of the exact type materialises
  nothing. A conversion (`out(5)` into an `output<double>`) builds the `T` on the stack up to
  `io::heap_copy_threshold` (4 KiB) and on the heap above it. Symmetrically in the input: a trivially
  copyable value is placed into storage with a single memcpy under the lock (its `move` is the same
  memcpy, so materialising it outside would be paying twice), whereas for a type with a real move the
  copy is still made **outside** the lock, so as not to drag an allocation into the critical section. A
  value the writer owns (an rvalue) is handed to the **last** subscriber by move, through
  `input_base::deliver_move` — whose default implementation copies, so an input kind that knows
  nothing about it stays correct.
- **There is no limit on value size.** No write path puts more than `heap_copy_threshold` on the
  stack, so a megabyte frame is a legal port type; hidden stack copies in the output and the input
  would crash the process with `STATUS_STACK_OVERFLOW` at **448 KiB** already (measured). Exactly one
  boundary remains and it is visible in the caller's own code: `get()` and `take()` return a **copy**
  into the caller's frame, so `auto v = in.get()` on a megabyte value puts a megabyte on the reader's
  stack — but that is a variable the reader declared.
- **A queue is always bounded.** `queued_input` takes a `queue_limit` — a number of items and an
  overflow policy — and "unbounded" cannot be expressed: `drop_oldest(n)` trims the head (live data:
  the consumer needs the fresher), `drop_incoming(n)` refuses the arrival without touching the queue (a
  continuous run matters more than its end). The default is `drop_oldest(32)`, so that adding a limit
  did not rewrite every declaration; the price is that a forgotten limit loses data silently instead of
  growing silently, and what makes it visible is the loss counter. Capacity is set **at construction
  only**: per-instance settings are properties, while a constructor argument is bound by the factory at
  registration, so different capacities are different registrations.
- **Why not blocking, and why not stopping.** `block` was rejected: the wait would sit on the writer's
  thread inside `deliver()`, introducing a deadlock for the legitimate "two modules feed each other"
  topology and requiring a wake on `stop_token`, or `stop()` hangs. `error` was rejected: the only way
  to bring the pipeline down from `store()` is to throw, and a throw from there tears delivery apart in
  the middle of the subscriber list (the first got the value, the third will not) and fells the module
  that writes rather than the one that overflowed. The loss counter covers both roles: it says the same
  thing and breaks nothing.
- **Every input counts losses, not only the queue.** `input_base::stats()` is pure virtual, beside
  `accepts()` and for the same reason: an input kind of your own must answer about itself rather than
  inherit silent zeros. `input_stats` is a dictionary of **values**, not of storage: `received`,
  `discarded`, `pending`, `peak_pending`, `capacity`. For "last value wins" the capacity is honestly
  one, and `discarded` counts overwrites of what was never taken — a loss otherwise visible through
  nothing at all. What is received is counted by the **non-virtual funnel** `operator()` rather than by
  `store()`: `store` is a documented extension point, an heir is not obliged to call the base, and an
  input kind that legitimately stores nothing would report zero received. Only `safe` instances are
  observable; `unsafe` answers zeros, as `write_count()` does on an unsafe output. There is no switch:
  the increment happens under a lock already held, and giving an operator a way to go blind to data
  loss would be a poor choice. Outward it goes through `group::input_metrics()` (a `port_stats` wrapper
  with a dotted path: a path is a runtime notion, and an input does not know it is inside a tree), then
  `runtime_view_base`, the MCP tool `read_input_metrics`, the "ports" table in the Runtime dock and the
  second table of `atp_app --metrics`.
- **Registries** (`detail::io_registry` → `inputs`/`outputs`/`properties`) own their items, and the
  heir declares them as references through `make<>("name", ...)`, whose trailing arguments go to the
  item's constructor as they are (`safety` for ports, the default and tags for properties). A record
  distinguishes ownership: `alias(name, port)` is a non-owning publication of somebody else's port
  (group ports), and `owned()` enumerates only the owned ones, which is the material for the runner's
  port→thread map. `get<TItem>` demands the exact dynamic type; `at`/`find` are the type-erased pair in
  the spirit of std::map. They are not thread-safe, being setup phase. Registries are movable: the
  ports live on the heap and a move transfers the storage without touching the objects themselves, so
  the heir's references and the connections stay valid.
- **Enumeration follows declaration order, and that is a contract rather than an observation.**
  `list()`/`entries()`/`owned()` return items in the order the author wrote them, so the MCP
  description, the studio inspector and the ports on a canvas node all show one order — the original —
  and none of them sorts by name. Hence a vector and a linear search, and **the price of that order is
  paid, not saved**: measured in Release on this tree, `find()` over four ports costs 10.7 ns against a
  hash map's 8.3 ns, and the gap widens with size (16 ports: 25 against 8; 64: 93 against 11). What
  makes the price right is **where** the lookups happen: wiring the pipeline and answering a
  description request — that is, setup and queries — whereas `iterate()` addresses ports through the
  heir's references and does not search at all. No module exists whose port count would make the gap
  noticeable; if one appears, the answer is an index beside the vector, not a return to hash order.
  Records move as the vector grows; the ports themselves never do, being behind `unique_ptr`, which is
  the same property movability rests on. The C path had this order from the start, its ports being
  declared as an array of descriptors (`bridges.md`, "Port indices are handed out in
  body-of-class declaration order").
- **Properties** (`property_codec.hpp`, `enum_names.hpp`, `property_base.hpp`, `property.hpp`,
  `properties.hpp`) are the third kind of entity a node declares: typed setting values with a default,
  edited live and read by the module pull-only.
  - `property_codec<T>` is the value↔string conversion trait: the statics `kind`
    (`property_kind {integer, real, boolean, text}` — **only** the form of the value), `to_string` and
    `from_string` (nullopt on something unparsable). The primary template is undefined: a type with no
    specialization is a compile error. Integers, floating-point types, `bool` and `std::string` are
    provided; the concept `property_value<T>` is the contract "this type is usable as a
    `property<T>`".
  - `enum_names<E>` is the "enum name table" customization point
    (`static constexpr std::array entries{enum_entry{E::a, "a"}, ...}`); the partial specialization of
    `property_codec<E>` for `named_enum` prints and parses names (kind `text`) and, beyond the common
    contract, declares `options()` — the permitted names in declaration order.
  - `property_base : io_base` gives type-erased access: `kind()`, `options()`, `persistent()`,
    `to_string()`/`from_string()` (which throws `std::invalid_argument` without touching either the
    value or the flag), `default_string()` (the studio does not write a value equal to the default into
    the config) and `changed()`. The instance tags are `persistent`/`transient`, in the style of
    `safe`/`unsafe`.
  - `property<T>` always has a value, so `get()` never throws; reading is pull-only: `get()` for
    state, `take()` for the event "changed since the last take" (a `reset()` to the default is an event
    too). Every write raises the flag: there is deliberately no comparison with the old value, so
    equality is not required of `T`.
  - **An enumeration** is not a separate kind of property but a non-empty `options()`. It is declared
    by two routes indistinguishable to consumers: a type-level name table (`enum_names<E>`) or an
    instance-level set — `make<property<int>>("channels", 2, allowed(1, 2, 6))`, available for any type
    (an instance set **replaces** the type table, which is how a module narrows an enum to the subset it
    supports). The invariant is that the value is always inside the set: the default, a typed write and
    `from_string` all pass one check against the canonical string, so `to_string()` never throws and the
    whole string layer — config, `-p`, the studio — is safe. `property_kind` does not depend on the set:
    an enumeration of numbers reaches the config as a number, one of enum names as a string; the
    inspector picks a widget by `kind` only for properties without a set.
  - **Integer and real are different kinds, and the split is paid for.** With a single `number` the
    form of a number has to be recovered by **running the printed string through a JSON parser**, and
    `value_fits` lets `5.5` into an integer property, deferring the refusal to `from_string`. The kind
    states the form itself, and the same codec prints and parses it (`property_codec<std::int64_t>` /
    `<double>`), so no second parsing implementation appears. The widening rule is the config's
    (`config_binding::read_scalar`): an integer will do for a real, never the reverse. The MCP wire says
    `integer`/`real` while a real's schema stays `"number"`, that being the only word JSON Schema has;
    `remote_runtime::kind_of` **tolerates the older `"number"`** and reads it as `real` — an integer read
    as a real edits and writes back without loss, whereas the other way round would put `"5.0"` into an
    integer property and be refused. A plugin compiles an enumeration's value into itself, so the
    extension is ABI-visible; the number did not move for the same reason it is still 1, and that is
    written into the `plugin_abi` docblock as a worked example of what, after the SDK is published, is
    answered with a 2. "One of a set" is still not a kind — it is `options()`.
- **The short `make<>`**: in the input and output sections `make<T>("name")` means
  `input<T>`/`output<T>`, and if `T` is itself a port then it is taken as is
  (`make<queued_input<int>>("events", drop_oldest(8))`); for properties `make("gain", 0.5)` deduces the
  type from the default, while the explicit form `make<std::string>("file", "", transient)` remains and
  is **not redundant** — deduction from `""` would give `const char*`. The spelled-out form is kept for
  `c_module`, where the type comes from a template parameter rather than from a source file.

## The module layer

- **`atp::ports<TIn, TOut, TProps>`** (`module/ports.hpp`) is a module's node: three sections — inputs,
  outputs and properties, ordinary registry heirs `inputs`/`outputs`/`properties` — passed to
  `module<>` as one parameter. The node reports its section types through the members
  `in_type`/`out_type`/`props_type`, and the requirement on the parameter is the `ports_list` concept.
  It lives here rather than in the io layer because it is about declaring a module: the io layer knows
  about sections, not about the shape they are gathered into. An input and an output may share a port
  name, the sections being separate registries. The node is movable, so it can be wired before the
  module exists and handed to the `module(TPorts)` constructor. Unneeded sections are omitted from the
  right (`ports<my_in>`, `ports<io::inputs, my_out>`); `ports<>` is the empty node.
- **`module_base`** is the type-erased contract: `module_context&` is given once, in `initialize` (and
  whoever needs it later stores the reference); `start()` and `stop()` take no parameters;
  `iterate(std::stop_token)` returns **`work_status`** (busy — there was or will be work, idle —
  nothing to do; the pacing signal for the runner); `inputs()`/`outputs()` expose the io registries
  through the base, without which connections by name are impossible; `properties()` exposes the
  property registry, which is the type-erased route taken by the builder, `-p` and the studio; plus
  `get_name()`/`get_version()`. The contract: **stop must be correct after initialize without start**,
  because fail-fast rollbacks call it.
  - The accessors are **pure virtual** rather than three pointers set by the heir: a forgotten pointer
    would be a null dereference on the first type-erased call, whereas a forgotten override is
    something the compiler does not let through. There is no hot path here — the connection machinery
    reads them at setup, the description tools on request, and `iterate()` goes into its own sections
    directly.
- **`module<TPorts = ports<>, Name = "", Version = 0.0.1>`**: ports and properties are passed as the
  single node `atp::ports<TIn, TOut, TProps>`, and `inputs()`/`outputs()`/`properties()` are covariant
  overrides, so a concrete module type sees its own sections (`inputs().step`, `outputs().count`,
  `properties().limit`) while the machinery sees the same registries type-erased through `module_base`.
  `module(TPorts)` accepts an already-wired node, which the heir opens up with `using module::module`.
  The name and version are declared once as NTTP parameters and are available both at compile time
  (`module_name`, `module_version`) and at run time. `version` is a structural type of up to four
  components with zero padding (`1.2 == 1.2.0`); `atp::ver<"1.2.3">` is consteval sugar. The default
  `iterate` is idle.
- **`module_host`** — the second and last thing in `module_context` — is the host's side facing **one**
  module. Two virtual functions, `log(level, string_view)` and `wake()`, both `noexcept` and
  allocation-free because `iterate` calls them; plus the non-virtual `error`/`warning`/`info`/`debug`,
  which can be extended without an ABI bump. **One field for both capabilities rather than two** — an
  extension point with an understandable lifetime, so that the next such service will need neither a
  new field nor a new bump. The key consequence for the cascade is that `module_context` is **per
  child** rather than one for the whole pipeline: `group::initialize` assembles it itself, substituting
  the shared `services` and the child's own `host`. That is why a log line knows its author although
  the author never names itself, and why `wake()` knows which thread to wake. The alternative of
  publishing both as services in `service_directory` — formally free of a bump — was rejected: services
  are resolved in `start()` while logging is needed already in `initialize`, the directory is not
  thread-safe and is meant for the setup phase, and the price would be a permanent possibility of
  getting a `nullptr` instead of a channel.
- **`service_directory`**, the first service in `module_context`, is a non-owning map from (provider
  name, interface type) to an interface. Type safety without dynamic_cast: `void*` plus a `type_index`
  key, ordered by type name, because the publisher and the consumer compute the key in different
  binaries. The protocol is **publish in initialize, look up in start**, which makes module
  initialization order irrelevant; removal happens in stop. The context is handed to the module only in
  `initialize`, so for lookup and removal it keeps a reference of its own. Thread safety of a published
  interface is its author's business.
- **Factories and the registry**: `module_factory<M, TArgs...>` binds the constructor's configuration
  **at the point of registration** (`add<M>(name, args...)` stores a tuple; all instances of one
  factory are identical, and different configurations are separate alias registrations).
  `module_registry` owns the factories, keyed by (name, version).
- **Per-instance parameters are properties**, not creation arguments: `create()` takes no parameters
  and values are set over the finished object (`runtime::apply_properties`, before `group::add` and
  `initialize`, so the module sees the settings already in `initialize`).

## Plugins (`plugin.hpp`, `module_loader.hpp`)

The umbrella `<atp/plugin.hpp>` is what a plugin includes; behind it sit the split
`plugin/abi.hpp` (the ABI number, the function types, the symbol names), `plugin/build_id.hpp`
(toolchain detection) and `plugin/handshake.hpp` (`ATP_PLUGIN_EXPORT`, `ATP_PLUGIN_HANDSHAKE()`).
CMake reads the ABI number out of `plugin/abi.hpp` as well.

- The DLL contract is two C symbols: `atp_abi_version()` (a pure C handshake, safe under any
  mismatch) and `atp_register_modules(registrar&)`. `plugin_abi` is 1, and an incompatible change to
  what a plugin sees is a bump. The requirement is one toolchain and a shared C++ runtime between the
  host and the plugins, with port types in shared headers with external linkage. Plugin file names are
  toolchain-agnostic (`PREFIX ""` plus `OUTPUT_NAME`: `atp_demo_plugin.dll` under any compiler) — that
  and the target's other properties are set by `atp_add_plugin()`, see `cmake.md`. A plugin path in a
  config may omit the extension: the platform one (`.dll`/`.so`/`.dylib`, the constant
  `atp::runtime::plugin_extension`) is appended by `module_loader`, the single point at which libraries
  are opened. Configs are cross-platform as a result — `"plugins": ["../plugins/atp_demo_plugin"]` in
  the demo has no extension, and the relative path resolves against the config's own directory, see
  `cmake.md`.
- **Pinning the DLL**: `module_ptr = unique_ptr<module_base, module_deleter>`, where the deleter
  carries a `shared_ptr` pin of the library (`plugin_library` is the RAII handle). Every module from a
  plugin holds its DLL against unloading, because a module may outlive the loader, while ownership
  stays unique for a deterministic teardown. The `pinned_factory` wrapper moves the module onto the pin
  at `create()`; the monolithic path, with an empty pin, is unchanged.
- **`atp_build_id()`** is a third, **optional** symbol: a string identifying the toolchain and the
  standard library (`_MSC_VER` plus `_ITERATOR_DEBUG_LEVEL`, or `__GNUC__` plus
  `_GLIBCXX_USE_CXX11_ABI`, plus a sanitizer marker). It catches the one incompatibility the ABI
  number cannot see: a Debug host and a Release plugin on MSVC differ in `_ITERATOR_DEBUG_LEVEL`, which
  is container layout, which both sides touch — that is memory corruption rather than a failed load.
  `atp_add_plugin()` checks only the static CRT and only at configure time, and a plugin from someone
  else's project does not go through that check at all. A missing symbol is tolerated silently (the
  loader has no host to warn through, and making an optional symbol mandatory is itself an ABI break),
  which is why adding the check needed no bump. Both symbols are emitted by the
  `ATP_PLUGIN_HANDSHAKE()` macro, having no plugin-specific content.

### What moves the number and what does not

The rule reads "bump on any ABI-incompatible change", and the key word in it is **ABI**. Renaming and
rearranging headers does not move the number as long as `module_base`'s layout, the set of virtual
functions and the sizes of types hold: a plugin already built still loads. What breaks is **source**
compatibility, and the cure for that is not the number but the absence of shims for old names and a
clean, declared break: a number raised for a rename starts to mean "rebuild", whereas its job is "do
not load what is already built".

The converse matters just as much: a purely **additive** change does not move it either. The optional
`atp_build_id()` symbol needed no bump, because a plugin that does not emit it loads as before; and a
config declared as data reached the C path without moving either `plugin_abi` or `ATP_C_ABI`, because
no data was added to the objects and no virtual functions either.

### Properties do not fill the config's niche

This is worth saying, or the two mechanisms read as one. A property is a **scalar**: it is visible in
the inspector, edited live, and has a codec to text and back. None of that helps a setting that is not
a scalar, is needed before `initialize` and is not edited live — that niche is filled by the config: a
structural value instead of a string, delivered to the constructor (`config.md`, "Transport:
`module_config`").

### The C path: modules in other languages (`plugin_c.h`, `c_module.hpp`)

A second registration path, added **beside** the C++ contract rather than instead of it: not one
header a C++ module author sees changed, and `plugin_abi` did not move when the C path appeared.

- **Why a second path at all.** The C++ contract is not an ABI but shared header-only code with a
  version number: `module_registrar::add` is inlined **into the plugin** and from there inserts nodes
  into the host's `unordered_map` with its own allocator, ports are instantiated in the plugin, and
  errors fly as exceptions. Hence the one-toolchain requirement, which is unavoidable — and removing it
  by rewriting the contract would take from C++ module authors exactly the ergonomics (typed `make<>`,
  RAII, exceptions) the SDK exists for.
- **Separating the sides** is the central decision. All the C++ stays on the host's side: a C-path
  plugin contains not one std type, instantiates not one port, and **frees nothing the host
  allocated**, or the reverse. That is what lets such a plugin be a Rust `cdylib` or a bridge with an
  embedded interpreter, with no C++ compiler at all, and it is also why `atp_build_id` is **not
  checked** for the C path: there is nothing for memory corruption to corrupt.
- **Three symbols, pulled rather than pushed**: `atp_c_abi_version()`, `atp_module_count()`,
  `atp_module_desc_at(index)`. The host fetches the descriptors itself instead of handing the plugin a
  registration callback: at load time not a single pointer to a host function crosses the boundary,
  reentrancy is impossible, and a plugin may be entirely static data. The pointers must live until the
  library is unloaded. The loader tries both paths independently, so a pure C plugin needs no
  `atp_abi_version`, and a hybrid plugin carrying both sets is legal.
- **Its own version, `ATP_C_ABI = 1`.** A C plugin does not implement the C++ contract and must not
  claim `plugin_abi`. That number will hardly grow: extension goes through `struct_size` fields in the
  descriptor and in the callback table — the host reads only the fields that fall inside the declared
  size — which is a mechanism the C++ path does not and cannot have. The first such field was `source`,
  and it also exposed what the mechanism was missing: the acceptance floor was `sizeof(atp_module_desc)`,
  the current size, which on the very first growth would have rejected exactly the plugins `struct_size`
  exists for. The floor is now the frozen constant `ATP_MODULE_DESC_SIZE_V1`, tied to the first field
  after version 1, and every new field is read only after a size comparison (`detail::c_desc_source`).
- **`source` is the file a module is declared in**, or NULL when there is no such file, which for a
  compiled plugin is always. The field is needed because a module's name is not a file's name and the
  host cannot guess it: the script `t_declares.py` declares a module `py_declares`, and in another
  folder `py_test3.py` declares `py_test311`. Only the side that read the file knows it. The value then
  travels beside the registration rather than in the factory (the loader's `registered_module`): a
  factory is a plugin-ABI type and an extra field in it would cost a `plugin_abi` bump, whereas the
  loader's list is the host's and costs nothing. `describe(factory)` therefore does not know the
  source — `module_manager::load_plugin` fills it in — and in MCP it is visible in `list_plugins` but
  not in `list_modules`. The studio uses it to give a module's row in the plugins dock a context menu
  with "open in editor" and "copy path", and the same item exists on a canvas node and in the object
  tree. A project node, however, names a factory rather than a file, so the reverse pass over the loaded
  plugins' rows is done by `module_manager::module_source(name, version)`, and the result is glued onto
  the description in `app_state::describe_cached` — once per cache entry, which dies on every scan
  anyway, instead of a pass on every opening of the menu. The item is shown only when the file exists:
  a module from a compiled plugin has none, and then the item is absent rather than greyed out.
- **A closed set of port types** (`atp_kind`: i32, i64, f64, bool, text, blob) is the one deliberate
  narrowing. Every value names a concrete C++ type, so a C module's port is **an ordinary platform
  port** and connects to a C++ module's port with no adapter in the config. `blob` is the escape hatch
  for everything else, and it has to be a real C++ type (`io::blob` = `std::vector<std::byte>`,
  `include/atp/io/blob.hpp`), or a C++ neighbour could not name the matching type. A trap: `i64` is
  `std::int64_t`, not `long` — on LP64 those are different `typeid`s.
- **Ports are declared statically**, in the descriptor rather than at initialization. That is a
  requirement, not a convenience: the host creates modules, applies properties and connects ports, and
  only then initializes — and a port that does not exist until the first connection cannot be connected.
- **Every C-path port is `safe`.** The point of `unsafe` is to take a mutex off the hot path, and a
  path that has already paid for an FFI call saves nothing on a mutex.
- **Value ownership is one rule: no allocation crosses the boundary.** Host to module
  (`get_input`/`take_input`/`get_property`/`take_property`): the bytes live until the next **reading**
  call on the same context, or until `iterate` returns. The restriction is on reading calls
  specifically, rather than on any call, so that the most natural thing a bridge does — read a payload
  and hand it straight to `write_output` — is legal; holding two payloads at once is not possible,
  because there is one buffer per instance, and that is also what makes reading a port
  allocation-free. Module to host (`write_output`/`set_property`): the bytes are needed only for the
  duration of the call and the host copies, which is the same contract the io layer already gives its
  writers.
- **The boundary is exception-free in both directions.** Outward: the other side reports the text
  through `set_error` and returns a non-zero code, and the adapter turns that into a
  `std::runtime_error` — after which the existing machinery works (fail-fast with a reverse `stop` in
  `group::initialize`, first-error-wins in the runner). Inward: every callback is `noexcept` and turns
  what would otherwise be an exception into a refusal plus a **deferred** exception, which the adapter
  throws after the foreign `iterate` returns, regardless of what it returned — otherwise a module that
  ignores a failed write would swallow the host's error. A foreign runtime that unwinds on its own (a
  Rust panic) must be stopped before the boundary.
- **`c_module` is the second hand-written heir of `module_base` after `group`**, for the same reason:
  the ports are dynamic, and `module<TPorts, Name, Version>`, whose declaration *is* its type, does not
  apply. Kind → type is unrolled by a single `switch` (`with_c_kind`), and reading and writing go
  through per-type function tables (`c_input_vtable_of<T>` and its neighbours) — the same technique as
  `input_base::erased_of<T>()`, that is, without a single `dynamic_cast`. Pinning the DLL comes for
  free: the factories are registered through `module_registrar::add`, which wraps them in
  `pinned_factory` itself.
- **Store the reference out of the context, not the context.** `group::initialize` builds a
  `module_context` for every child **on its own stack** for the duration of the call, so the adapter
  remembers a `module_host*` rather than a `module_context*`; a stored address of the aggregate points
  at a dead stack by the time `start()` runs — in practice at a neighbouring module's context, and the
  log line goes to the wrong author.
- **`blob` cannot be a property**: a property is a scalar a human edits as text, and there is
  deliberately no `property_codec` for bytes. The refusal is needed both at load time and at compile
  time: the dispatch instantiates all six kinds regardless of what the descriptor asks for, so a
  run-time check alone is not enough.
- **A mirror of the header on the other side.** A plugin not written in C cannot include `plugin_c.h`
  and repeats the declarations in its own language (`templates/plugin_rust/src/abi.rs`). Such a mirror
  is right only while the layout it was written against holds, and **reordering or retyping a field in
  the header breaks it silently**: every C++ consumer recompiles and stays right, while foreign mirrors
  start reading the wrong bytes. Hence `tests/platform/plugin_c_layout_tests.cpp`, a set of
  `static_assert`s on sizes and offsets that turns this into a build error next to the header itself
  (for LP64/LLP64, i.e. for every platform this builds on; elsewhere the checks pass vacuously rather
  than pretend to know the answer). `struct_size` catches only a structure **shorter** than expected.
- **A price paid deliberately.** `module_loader.hpp` includes `c_module.hpp`, which instantiates ports
  for all six kinds, and every `output<T>` drags `std::any` boxing in from `erased_of<T>()`. MSVC emits
  those COMDATs into every TU that includes the loader, whether it calls anything or not: the object
  file grows from 25 to 488 sections (measured), and `atp_tests` together with `src/mcp/main.cpp`
  crossed the obj format's limit. Hence `/bigobj` **on `atp_runtime`** as an INTERFACE option: the flag
  is a technical consequence of a header living here rather than a policy, which is why it is not in
  `atp_warnings`, and it reaches exactly the targets that include the loader and no further —
  `atp_runtime` is deliberately not exported, so a plugin author's build does not inherit it. The linker
  folds the duplicates and the binary is unaffected; the price is compile time. gcc and clang have no
  such limit.
