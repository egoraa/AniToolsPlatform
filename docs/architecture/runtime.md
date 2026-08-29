# Execution platform and host (`src/runtime/`, `src/app/`)

What executes a pipeline: the composite, the runner with its named threads, the log channel and the
metrics — plus `atp_app`, a thin host driven by a JSON config, and its control channel. The config
document itself is covered in `config.md`. The document map and the tree layout are in
`../architecture.md`.

## Execution platform (`src/runtime/`)

- **`group : module_base`** is a composite module: it owns its children
  (`child{name, module_ptr, detached, counters, host, subgroup}`, a subgroup being just another child)
  and looks like an ordinary module from outside. **A composite is recognised once, in `add()`** — the
  single funnel through which a child enters a group, `make<>` and `add_group()` included — and the
  answer lives in the record: eight tree walks read `child::subgroup` instead of a
  `dynamic_cast<group*>` each. Whoever puts the child in already knows whether it is a group; the
  point of the field is not to throw that knowledge away and grope for it later. The `dynamic_cast`
  that remains in `add()` is deliberate: the type-erased overload accepts the result of
  `module_registry::create()`, and a group can arrive through it, so recognising by static type in
  `make<>` would leave the invariant resting on an unwritten agreement that groups are not passed to
  `add()`. RTTI is safe here by construction: `group.hpp` lives in `src/runtime/include` and is not
  exported in the SDK, and a plugin links `atp::platform` alone, so the group's typeinfo exists in
  exactly one binary — the host's. The lifecycle is recursive cascades in insertion order:
  `initialize` is a local fail-fast (a child throws → stop the already-initialized in reverse order,
  rethrow); `start` has no rollback (the runner calls `root.stop()`); `stop` runs in reverse order,
  continues on error and rethrows the first; `iterate` aggregates busy-wins and skips detached
  children, which have a thread of their own. A group's ports are **aliases** to its children's ports
  in its own registries (`expose_input`/`expose_output`, path form `"child.port"` only; a re-export
  resolves straight to the real port). `connect(from, to)` works over paths; the `{out, in}` records
  own nothing; the destructor breaks its own connections before the children are destroyed. A group is
  **not** a unit of execution — the runner's layout decides that — and is **not** thread-safe, being
  setup-phase machinery.
- **The log channel and wake-up** (`host_node`, `log_ring`, `log_pump`) implement `module_host` on the
  runtime side. The node sits in `group::child` beside the module: it is created with it and dies with
  it, which means it **outlives the pipeline**, and that is a requirement rather than a convenience —
  an OS callback may call `wake()` during shutdown, and the handle must absorb that instead of
  dangling.

  **The ring** (64 slots of 256 bytes; the numbers are runtime-side and change without a bump) follows
  Vyukov's ticket discipline: every slot has its own sequence number and a ticket is taken **only**
  once the slot is already free. The naive scheme — take a ticket, then drop the record if the slot is
  busy — hangs the reader forever: the ticket is spent, no publication follows, and the cursor stops on
  a hole. On overflow the **new** record is dropped rather than the oldest: overwriting would race the
  reader for a worse result, since in a flood what matters is when it started, not the last 64
  identical complaints. Losses are counted, an over-long line is cut with a flag, and both losses are
  visible in the output.

  **The moment of writing** rides in the slot beside the level, and the clock (`system_clock`) is read
  **once, before the ticket is taken**: under contention "when the module called `log()`" and "when it
  won a slot" are different moments, and a log reader wants the first. The time is taken by the writing
  thread rather than by whoever drains, and that is the heart of the decision: draining runs on a timer
  (50 ms in the pump, 10 Hz in the studio), so its own clock would collapse a whole batch of lines into
  one instant and place it later than everything happened. The price is one clock read per line, which
  on a diagnostic channel is nothing next to copying the text beside it. `format_log_time` renders it:
  local time to milliseconds, with no date (a log is read the same day, and a date would cost width on
  every line) and no microseconds (order within a batch is honest anyway — the ring hands lines back
  oldest first). On a machine with no timezone database the conversion throws, and the function falls
  back to UTC instead: a formatter that dies in the middle of a report about trouble destroys the
  report as well.

  **Wake-up** is attached in `install_notifiers()` — the one place that knows which thread runs a
  module — and removed in `uninstall_notifiers()`. Only for `on_demand`: under `throttled` the same
  `cv` waits with `wait_until`, and an early wake would break the very period the mode exists for. A
  subgroup child handed to another thread is signalled on **its own** thread, not the parent's.
  Measured: a median of ~44 µs against roughly 5 ms that an external event waited before
  (`docs/benchmarks.md`).

  **The pump** is needed by console hosts, because their main threads have nothing to drain with:
  `atp_app` stands in `runner.wait()` and `atp_mcp` in reading stdin. The studio does not need it — it
  drains from its own `QTimer`. The pump's source is a function rather than a `pipeline&`: in
  `atp_mcp` there is no pipeline in `main` at all, a tool call builds one. **The memory ordering in the
  ring was verified by reasoning and by a four-producer stress test, not by a race detector** — TSan is
  unavailable on MSVC, and the first live `sanitizer / thread` run looks here.
- **Per-module metrics** (`set_metrics_enabled`, `metrics()`) answer the question "which module is
  eating the time", which the runner's pass counters cannot: a thread drives an ordered list of groups,
  and one slow `iterate` among twenty looks like twenty slightly slow ones. The composite times every
  call into a child and accumulates `calls`/`busy`/`total`/`max` in relaxed atomics — the same
  discipline as the runner's counters, a monitor needing a fresh number rather than a frame-accurate
  one. **Off by default, and that is a decision by the numbers rather than out of caution:** a pair of
  `steady_clock::now()` calls per child per pass dropped a two-module pipeline from 4.49 to 3.35 M
  items/s — a quarter of the throughput, because a cheap `iterate` is cheaper than two clock reads. The
  disabled state costs one relaxed load and a predictable branch, which is within the noise. A
  subgroup's time is the time of its whole subtree minus the detached children the cascade skips, and
  the difference between a group and the sum of its children is the group's own overhead. Metrics
  surface through `session::module_metrics()` in the headless studio core — one implementation for two
  consumers, the Qt studio and MCP — feeding the studio's Runtime panel (the "modules" section: a
  measurement toggle and a table sorted by total time, the toggle live only on a running pipeline
  because the counters live in the tree the runner drives) and the MCP tools
  `set_module_metrics`/`read_module_metrics`; and separately through `atp_app --metrics`, which prints
  a table on shutdown. Beside it, `--run-for <ms>`: without a bounded run a measurement is neither
  reproducible nor scriptable, and on Windows there is simply no way to send Ctrl+C to a console
  application whose output is redirected.
- **`pipeline`** is the aggregate root: a group named `"root"` plus `service_directory` plus
  `module_context`. No threads; member order guarantees the group dies before the services.
- **`pipeline_runner`** is execution. **Named threads** with modes: `on_demand` (sleeps when idle:
  yield, then doubling 1→10 ms, reset by busy), `throttled` (a fixed period, with lag allowed to slide
  rather than be made up) and `spinning` (pure yield, for the latency-critical). With no `add_thread`
  at all there is an implicit `"main"`; an unassigned root goes to the first declared thread; an
  unassigned group runs inline under its nearest assigned ancestor; a thread with no groups is not
  created. The thread's name reaches the OS and the text of errors. `start()` builds a group→thread
  map, then a port→thread map (from the modules' `owned()`, so registries holding only aliases drop out
  by themselves), then **validates**: an unsafe input across a thread boundary is an error naming both
  threads (the criterion is the thread boundary, not the group one); then it detaches assigned
  subgroups, cascades through the root and starts the loops. Any failure in `start()` rolls everything
  back — not running means the state is clean. **Wake-on-delivery**: `start()` attaches a notifier
  (`thread_signal`) to inputs whose connections cross into an on-demand thread, so delivery wakes the
  sleeping consumer immediately and resets the backoff; throttled and spinning are untouched, and the
  notifiers are removed on shutdown after the threads are joined. Only connections made through
  `group::connect` are visible — a direct `out.connect(in)` behind the group's back does not wake
  anything, the same contract the cross-thread validation has. On an execution error the first one
  wins and `request_stop` goes to the whole pipeline (under the lock, being a CV predicate); `wait()`
  means "run until something breaks": a CV wait, then shutdown, then a rethrow of the root cause
  (including after a prior `stop()`; the error is kept until the next `start()`); `stop()` is
  idempotent and never throws, being called by the destructor. `stats()` is a snapshot of per-thread
  pass counters (passes/busy_passes, relaxed atomics): empty until the first `start()`, alive until the
  next, readable by the owner at any time including on a running pipeline. **All runner control is
  owner-thread-only** — the stop/wait race is excluded by contract, not by synchronization.

## The atp_app host (`src/app/`)

- A host driven by a JSON config: `atp_app <config.json>` loads, validates, builds a `pipeline` plus a
  `pipeline_runner` and runs until Ctrl+C or a failure. `atp_app` is a thin `main.cpp` over
  `atp_runtime`; the tests link `atp_runtime` directly.
- The components (`src/runtime/include/atp/runtime/`, included as `<atp/runtime/...>`, namespace
  `atp::runtime`): `config_loader` (reading plus `$include`), `config_validator` (every error in one
  pass, each with its JSON path), `config_model` (the typed model plus `decode` and its inverse
  `encode`, whose canonical form omits defaults), `pipeline_builder` (`application`; `build` does
  plugins plus assembly, `build_pipeline` assembles into an already-populated registry, which is the
  studio's path) and `property_override` (parsing and applying a "path.prop=value" edit:
  `parse_property_override`, `find_property` descending the group tree, `apply_property_override`; every
  refusal is a `config_error`).
- **Schema 1.0** (`config_schema_version`): a config's major must match and its minor must not exceed
  ours, so fields from the future are rejected rather than ignored. A key added so that the shape of
  the existing ones does not change costs a minor: a document of the previous minor still reads
  unchanged, while a document naming a key from the future is refused on the old host by version rather
  than failing somewhere obscure. A key that disappears or changes shape costs a major, and then the
  document is rejected whole instead of being read with a key silently dropped. Separate tests pin that
  `children` and `replay` stay unknown keys: a subgroup is declared as another module under `modules`,
  and the output cache that `replay` needed does not exist. The root keys are `version` (in the root
  document only), `plugins` (paths relative to the config's directory — a file **or a directory**),
  `pipeline` (a tree of `modules`, each `{module,name,version,properties}` or
  `{group,modules,expose,connections}`, where `expose.inputs`/`outputs` map an alias to `"child.port"`
  and `connections` are `{from,to}`), `threads` (`{name,mode,period_ms}`) and `assign` (a group path to
  a thread name). An unknown key is an error, so typos do not vanish silently.
- **`assign` addresses subgroups only, never the root.** A path in `assign` is parsed from `pipeline`
  downwards by subgroup name (`config_validator::group_path_exists`,
  `pipeline_builder::resolve_group`), and the root group has no name in the document — `decode` calls it
  `"root"` itself. The root therefore always runs on the **first declared thread**
  (`pipeline_runner::start`), and the way to put it on another is to declare that thread first in
  `threads`. A reserved name for the root was rejected: it would immediately collide with a subgroup an
  author named the same.
- **An entry of `plugins` may be a directory**, and then the plugins lying directly in it are loaded.
  Without recursion — the same way the studio scans (`module_manager::rescan`), so that one directory
  yields the same set in both places. The walk is sorted by file name: `directory_iterator` promises no
  order, and the order decides which plugin claims a module name first and which of two conflicting
  ones gets the error. Paths are deduplicated by `weakly_canonical`, so a config naming both a
  directory and a file inside it does not load the same thing twice.
- **A directory's failures and a file's failures differ, and the difference lives in the loader.** A
  file that opened but exports neither `atp_abi_version` nor `atp_c_abi_version` is
  `atp::runtime::not_a_plugin`, and it is skipped while walking a directory: a directory means
  "whatever is found in it", and someone else's library sitting there must not bring the host down.
  Every other failure stops the host — including a file that **did not open at all** (a corrupt image,
  the wrong word size, a missing dependency): the symbol check was never reached, so the file cannot be
  called someone else's, and that is the most common real breakage. A file named explicitly is
  forgiven nothing: the config promised that plugin is there. `not_a_plugin` derives from
  `std::runtime_error`, so a caller that catches the base keeps working.
- **`$include`**: `{"$include": "path.json"}` in any node is replaced by the file's contents (paths
  relative to the including file; sibling keys forbidden; cycles and a depth above 16 are errors;
  `version` is forbidden in a fragment).
- Platform build errors are wrapped in config context (`config_error`: which module, group or
  connection). Exit codes: 0 for a normal shutdown (Ctrl+C), 1 for a pipeline or build failure, 2 for
  usage, an invalid config or a bad `-p`.
- A module's `properties` is an object of name → JSON scalar (number, string, bool; nesting is a
  validator error). The model stores nodes rather than strings: `encode` has to tell `5` from `"5"`.
  The builder turns a scalar into a string (`scalar_to_string`) and hands it to
  `property_base::from_string` — an unknown name and an unparsable or disallowed value become a
  `config_error` (the property's own message already names it and lists the permitted values); a
  non-scalar value is cut off before that, and the error names the property and the form it found.
- **A real's trailing `.0` is load-bearing, not cosmetic.** `scalar_to_string` prints the scalar
  itself (`std::to_chars`) rather than going through `json_dump`, which would raise a whole document
  tree for one number; but it does append the tail, and here is why. `to_chars` prints `48000.0` as
  `48000`, and `property_codec<int>` **accepts** such a string — so a real from the config would
  silently fill an integer property, erasing exactly the distinction `config::node` keeps two forms
  for. With the tail, `from_chars` stops at the dot and the property refuses. `inf` and `nan` need no
  case of their own: they become `inf.0` and `nan.0`, which nobody accepts.
- **`atp_app -p path.prop=value`** (the flag repeats) applies edits over the config before
  `runner.start`, so a module sees them in `initialize`. It splits on the **first** `=` (a value may
  contain one) and on the **last** `.` to the left of it (a property name holds no dot, a path may).
- The demo: `src/app/config/demo.json` (plus `demo_consumers.json` through `$include`) wires counter →
  scaler (throttled, 500 ms) → printer from `atp_demo_plugin`. The connections are deliberately of
  mixed types (`int`, `std::string`, `double`, a user-defined `demo::sample` and the universal
  `std::any`), and one label travels two edges at once, into a typed input and into a universal one. A
  custom target copies the configs and the DLL next to the binary on every build.

## The control channel (`--control <port>`)

- **Why.** A deployed host is the only one that runs with nobody beside it, and without this channel it
  is the deafest of the three: `-p` applies before `runner.start`, which makes it a launch parameter
  rather than a control surface, and a running process leaves you nothing but reading its output and
  killing it.
- **The same protocol, not a second one.** What goes over the wire is exactly the MCP that `atp_mcp`
  speaks; `server::handle` is a pure `json → json` function, and `atp_app`'s channel is its second
  consumer, which is what it was made pure for. The tools over a live pipeline (`get_status`,
  `describe_pipeline`, `read_connections`, `set_module_metrics`, `read_module_metrics`,
  `set_live_property`) are declared once in `mcp/control_tools.hpp` as a template over the
  `control_target` concept: `studio::session` satisfies it as written, and `mcp::application_control`
  gives the same shape over `runtime::application`. A concept rather than an abstract base, so as not
  to add virtual functions to `session` for the sake of one second implementation. `run` and
  `sync_persistent_properties` are not offered on the channel: the host has no document.
- **`stop` is deliberately not shared.** In `atp_mcp` the word stops the run and leaves the server
  alive; in `atp_app` it takes the process down. Same name, different scope of action, so
  `register_shutdown_tool` takes a callback: the tool only asks, and what "exit" means is `main`'s
  decision. A side benefit is that the tool is testable without killing the test process.
- **A queue rather than locks.** All `pipeline_runner` control is owner-thread-only. The studio honours
  that by having the GUI thread be the owner; in a headless host the request arrives on a transport
  thread, so `runtime::command_queue` moves **every** call — reading ones included — onto the main
  thread and returns the result or the exception. Separating readers from writers would mean reasoning
  about each tool separately, and one mistake in such reasoning gives a race nobody sees for a long
  time. The host's wait loop became `queue.run_pending(50 ms)` instead of a sleep, so a request wakes
  it at once.
- **Loopback TCP rather than a socket file or a named pipe.** One code path across four systems,
  against two implementations for the unix-socket/named-pipe pair and against the uneven behaviour of
  `AF_UNIX` on Windows and in containers. A port of `0` means "pick a free one and print it", which
  also removes the race for a port in tests. Readiness is awaited with `poll`/`WSAPoll` and a timeout
  rather than `select`: the `FD_SET` macros warn under `-Werror`, and closing a socket from another
  thread does not reliably wake `accept` on Linux, so a flag plus a bounded wait is not an optimisation
  but the only portable way out.
- **The channel is unauthenticated, and that is a decision rather than an oversight.** Binding to
  `127.0.0.1` keeps it off the network, but any local process can connect and, among other things,
  shut the host down: TCP has neither filesystem permissions nor a portable way to ask who is on the
  other end. So the endpoint is **off** until an operator names a port, and a warning is printed at
  startup. Where that is unacceptable, `--control` is simply not passed.
- One client at a time: multiplexing would raise a question the scope has no answer to — what two
  clients editing one property means — while the `listen` backlog already makes the second wait rather
  than fail.
- **The port line is printed to stderr in one piece.** Modules are allowed to print (the demo plugin
  does) and do so to stdout from their own threads, so an announcement assembled from several `<<`
  physically tears in half — observed live as `control channel on 127.0.0.1:` + `printer label: tick
  100` + `55637 (...)`. No port can be read out of that, and `--control 0` exists for precisely that
  reading. Diagnostics on stderr is the same boundary as in `atp_mcp`, where stdout carries the
  protocol.
