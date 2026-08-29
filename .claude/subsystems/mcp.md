# atp_mcp

`atp_mcp` is a headless **MCP server over stdio** on top of the studio core (`src/mcp`, logic in the
header-only `atp_mcp_lib`, included as `<atp/mcp/...>`, also linked into `atp_tests`): it exposes the module
catalog, document editing and pipeline execution as MCP tools, so an agent can build a pipeline, run it and
read the values on the connections. Protocol revision `2025-11-25`, newline-delimited JSON — **stdout carries
the protocol and nothing else**, diagnostics go to stderr. Flags (parsed by `atp::mcp::parse_options` in
`options.hpp`, so the command line is testable without a process): `--root <dir>` (workspace root every config
path is confined to, default CWD), repeatable `--plugin-dir <dir>` (extra directories plugins may be loaded
from — loading a plugin runs foreign code, so that policy is separate from the config one) and repeatable
`--scan-dir <dir>` (directories scanned for plugins at startup, so the catalog is populated before the first
`list_modules`; a scan dir is trusted, hence also a plugin dir). Directory arguments are resolved against the
process CWD, not the root — they come from the operator, not the client. Unlike the Qt studio, the server keeps
no settings file, so search dirs added at runtime via `add_plugin_search_dir` do not survive a restart;
`--scan-dir` is the persistent form. `server::handle` is a pure `json → json` function, which is how the whole
protocol is tested without spawning a process.

**The `"config"` key is printed on `module_info::takes_config` alone**, and filled by walking
`module_config::entry` — there is no `module_declaration::config_schema` to read. So an empty
`"fields"` means "takes a config it does not describe" (a `raw_config`, or
`using config_type = atp::module_config;`), and no key at all means it takes none.

Naming trap: the object studio edits is `atp::studio::project` (`studio/project.hpp`), but the **wire
vocabulary here is "document"** — tools
`new_document`/`open_document`/`save_document`/`get_document`, the `"document"` result key, the
`atp://document` resource and `mcp/document_tools.hpp` all keep their names, while `workspace` exposes
the object as `project()` with its path as `project_path()`/`project_dir()`.
