# Rust plugin template

A platform module written in Rust and loaded through the **C path** of the ABI
(`include/atp/plugin_c.h`). There is no C++ compiler here, no installed SDK, no `find_package` and not
a single line of C++: the whole contract is `src/abi.rs` (a mirror of the header) and three exported
C functions. Copy the directory anywhere — nothing here points outside it.

```bash
cargo build --release
```

That is all. There is no SDK installation step: the header is for the person checking `src/abi.rs`
against it, not for the build.

## Putting it next to the host

Cargo produces a file with a platform-specific name, and on Unix it is **not** the name the platform
expects: plugin names there are toolchain-agnostic and carry no `lib` prefix (see `atp_add_plugin`),
while cargo adds one for a `cdylib` and cannot be told not to. Hence one renaming step:

```bash
# Linux
cp target/release/libatp_rust_plugin.so <next to atp_app>/plugins/atp_rust_plugin.so
# macOS
cp target/release/libatp_rust_plugin.dylib <next to atp_app>/plugins/atp_rust_plugin.dylib
# Windows — cargo already produced the right name
copy target\release\atp_rust_plugin.dll <next to atp_app>\plugins\
```

This keeps `"plugins": ["../plugins/atp_rust_plugin"]` in the config cross-platform: `module_loader`
appends the extension. The path is relative to the **config**, which is why it starts with `../`: the
configs sit in `config/` and everything loadable in `plugins/` beside it. The alternative is to spell
the full file name in the config, but then the config stops being portable — which is the very
property toolchain-agnostic plugin names exist for.

## ABI version

For a C++ plugin `atp_require_plugin_abi(N)` takes care of this and fails **configuration**. There is
no CMake here, so there is one check and it happens at load: `atp_c_abi_version()` returns
`ATP_C_ABI` from `src/abi.rs`, and the host refuses to load a plugin carrying a different number,
naming both.

`src/abi.rs` is written by hand rather than generated: bindgen would make a C toolchain and a build
script a dependency of every plugin, taking away exactly the independence the C path exists for. The
price is keeping the file in agreement with the header. What that does catch: the `struct_size` field
lets the host see a structure **shorter** than the expected one, so a forgotten new field is a clear
error at load. A reordered or retyped field is caught by nothing, so when `ATP_C_ABI` changes, check
against the header rather than against this file.

## Reading the config

The config is the second channel beside properties: a structure — a list, a table, a nested object —
that the module is given at creation and that nobody edits while it runs. `rust_averager` takes a
`weights` list that way, while `window` stays a property:

```json
{
    "module": "rust_averager",
    "name": "mean",
    "properties": { "window": 4, "mode": "verbose" },
    "config": { "weights": [1, 2, 3, 4] }
}
```

`read_weights` in `src/lib.rs` is called from `create`, which is where a config may be read: it is
the C path's analogue of a constructor, and the channel exists for what has to be known before a port
is connected. Nothing is allocated across the boundary — the tree is walked through handles
(`config_root`, `config_find`, `config_child_at`, `config_value_of`) and copied into a `Vec` here.

Two rules the function shows. The accessors were **appended** to `AtpApi` after `ATP_C_ABI` 1, so
`api.has_config()` is asked before any of them is called — see the comment on that method for why its
threshold is not `size_of::<AtpApi>()`. And every read degrades to an empty vector rather than
failing: the node may have named no config at all, and a module that fell over on that would be a
module nobody could place without first giving it something it refuses to run without.

Four more accessors were appended later still, and they have a detector of their own for exactly that
reason — `api.has_config_text()`, never `has_config()`, which a host carrying only the first seven
answers yes to. They are `config_find_path` (a whole dotted path in one call, followed only from the
root), and `config_text` / `config_origin` / `config_is_opaque` for a config the pipeline attached as a
file in a format the host does not parse: the bytes arrive verbatim and the module parses them itself.

## A panic is not "just an error"

A panic unwinding into a C++ frame is undefined behaviour, and it is the main thing to take away from
`src/lib.rs`: every entry point is wrapped in `catch_unwind`, and the wrapper that catches one
reports it through `set_error` as an ordinary module error. The pipeline then stops the normal way,
with the panic text in the message.

The alternative — `panic = "abort"` in the profile — closes the undefined behaviour too, but takes
the host process with it and loses the diagnosis, which is why it is not used here. You can watch it
on a live pipeline: the module has a transient `panic` property, and
`atp_app -p mean.panic=true pipeline.json` shows the whole path.

## What this checks besides building

`pipeline.json` puts this plugin's module between two modules of `atp_demo_plugin`: `counter` →
`rust_averager` → `printer`. Both ends of every connection come from libraries built by **different
toolchains in different languages**, and the ports are ordinary platform ports — there is no adapter
in the config. The input takes an `int`, the outputs hand over a `std::string` and a `double`, so
this exercises both recognising a type across the boundary and having a string buffer freed by the
side that did not allocate it.

```bash
atp_app pipeline.json      # prints "printer label: rust_averager: N -> mean M over K of 4"
                           # and "sink.measure: M" — the second line arrives through another port
```

The set of port payload types on the C path is closed (integers, `double`, `bool`, text, and a byte
blob as the escape hatch for everything else) — the one deliberate narrowing compared with the C++
path, where a port type can be anything at all. The matching C++ type for a blob is `atp::io::blob`.

## License

The template is part of AniToolsPlatform and is covered by the Apache License 2.0: a copy of the text
sits in `LICENSE` beside it, copyright 2026 The AniToolsPlatform Authors. The
`// SPDX-License-Identifier: Apache-2.0` lines in `src/*.rs` apply to the code of the **template**,
not to whatever grows out of it.

License your own plugin however you like, up to entirely closed: `plugin_c.h` is a header under
Apache-2.0, and a work derived from it is under no obligation to be open. Once you have copied the
directory, replace `LICENSE`, the `license` field in `Cargo.toml` and the SPDX lines with your own —
or your code ends up published under someone else's copyright. The one thing Apache-2.0 asks in
return is that you keep the notices in whichever template files you leave as they are (section 4(c)).
