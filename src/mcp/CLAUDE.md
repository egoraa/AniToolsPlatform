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
