# The host runtime

`src/runtime/include/atp/runtime/` is host machinery — everything a host or loader needs and a module
author does not. It is all `namespace atp::runtime` behind the umbrella `<atp/runtime.hpp>`, so an
unqualified name below is `atp::runtime::` and an unqualified header is this directory. The layer as a whole, its umbrella and what is deliberately outside it are described
in the root `.claude/CLAUDE.md`. What follows is the part a file in this directory can get wrong on its
own: the host side of the C path.

## The C path, host side

**A config may be declared from data rather than from a type.** `atp::dynamic_config`
(`module/dynamic_config.hpp`, umbrella **`hosting.hpp`** — its audience is a host, not a module author)
declares from a `field_kind` and a canonical default string, and `atp_module_desc` carries
`config_fields`/`config_field_count` behind `struct_size`, so a C plugin, a Python class or a Lua table
can say what its config is. `atp_config_field_desc` deliberately carries **no `struct_size`** — it is
read as an array, and a growable element type breaks the stride, which is why the three port descriptors
are frozen too. The base gained exactly **one** protected name for this, `declare(dynamic_field)`, and
no data member: an array of objects lives in `element_array` inside the existing `owned_`, so **neither
`plugin_abi` nor `ATP_C_ABI` moved**.

**`c_config` renders the tree back with `values_of`, never with `save_fields`.** The two are deliberate
opposites — `save_fields` thins defaults away, `values_of` keeps them — and merging them would strip
every module's defaults. Do not. `c_module` reaches the tree through the data-less mixin
`config_tree_source` (a common base would mean inheriting `module_config` twice), and the path grammar
lives in the free `find_path`/`at_path` of `config_path.hpp` rather than in `raw_config`.
`module_manager::describe` already drops only a `raw_config`, so a declared C config keeps its
`config_schema` and studio draws the tree. `docs/architecture/config.md`, "Declaring a config on
the C path".

**Eleven callbacks are appended to `atp_api` behind `struct_size`, with no `ATP_C_ABI` bump**, and the
last four (`config_find_path`, `config_text`, `config_origin`, `config_is_opaque`) are asked for by
**their own** detector `atp_api_has_config_text` — a host carrying the first seven answers yes to
`atp_api_has_config` and must answer no to this. Four traps:

- config text is valid for the module's **whole lifetime**, stronger than a port's, since it points
  into the host's tree rather than the shared scratch;
- the boolean `guarded` must not be used for a callback answering a handle — it reduces its body to 1
  or 0, which turns `ATP_CONFIG_OBJECT` into `ATP_CONFIG_BOOL` silently. Use `guarded_value<TRet>`;
- `config_find_path` follows a path **only from the root**, because the flat preorder index has no
  reverse mapping;
- a host-side error raised inside a callback during `desc.create` surfaces because the constructor
  calls `rethrow_pending()` after it — **destroying what the plugin already built first**, since a
  throw from a constructor runs no destructor and nothing else would ever call `desc.destroy`.

`docs/architecture/config.md`, "The C path" (not `sdk.md`'s "The C path: modules in other
languages", which is about the registration path itself).
