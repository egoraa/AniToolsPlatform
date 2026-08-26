# Plugin template

A minimal plugin project that builds **against an installed SDK** and knows nothing about the
platform's source tree. Copy the directory anywhere — nothing here points outside it.

```bash
# 1. install the SDK somewhere
cmake -S <platform> -B build
cmake --install build --prefix /somewhere/atp-sdk

# 2. build the plugin as a project of its own
cmake -S templates/plugin -B build-plugin -DCMAKE_PREFIX_PATH=/somewhere/atp-sdk
cmake --build build-plugin
```

`CMakeLists.txt` comes down to three substantive lines: `find_package(AniToolsPlatform)`,
`atp_require_plugin_abi(15)` and `atp_add_plugin()`. The second one is not a formality: when the
platform raises `plugin_abi`, configuration here has to fail until the plugin has been revisited.
Everything else — hidden visibility, the file name, the target type, linking `atp::platform` alone —
is set by `atp_add_plugin`, and those properties are not meant to be written out by hand.

## What this checks besides building

`pipeline.json` connects this plugin's module to modules of `atp_demo_plugin`: `counter` → `doubler`
→ `printer`. Both ends of every connection come from **different libraries**, built by different
projects — the only place in the repository where that is so. The input takes an `int`, which every
library agrees about in advance; the output hands over a `std::string`, and that is what exercises
the real thing: recognising a type by name across the boundary, and freeing a buffer on the side that
did not allocate it.

## The three ways a setting reaches the module

`doubler` declares all of them, and which one a setting belongs in is decided by the setting, not by
taste:

- a **property** — `factor`, one scalar with a default, editable while the pipeline runs and read
  pull-only from `iterate`;
- a **config** — the `bands` list, a structure no property could hold, handed to the constructor
  before `connect` and `initialize` and never edited afterwards. A module joins that channel by naming
  its config type and taking ownership of one, `std::unique_ptr<doubler_config>`; one that names none
  is created exactly as it was before the channel existed;
- an **input** — `value`, the data itself.

The config here is **declared**, not parsed: `doubler_config` derives `atp::module_config` and names
its fields as members — `list<band_config>("bands")` for the table, `field("otherwise", "large")` for
the scalar — and the module names the type as `using config_type = doubler_config;`. Three things come
with that declaration. The fields are filled before the constructor body runs, so there is no loop and
no per-key fallback to write. The document is checked against the schema **before** the module is
built, and every problem is reported at once, naming the file and the path (`bands[1].upto`) instead of
stopping at the first. And the host can read the shape without building anything, which is how an
editor draws a form for a config instead of a box of JSON.

Declaring no field at all is the other half of the channel: `using config_type = atp::module_config;`
says the module takes a config and reads none of it through fields. That is what a module wants when
the pipeline attaches a file the platform does not parse (`"config": "file:rig.ini"`) — `config->is_opaque()`
is then true and the bytes are in `config->text()` with the file's name in `config->origin()`, so the
module parses its own format and the platform learns none.

Running it (the plugin goes into `plugins/` next to `atp_app`, the config into `config/` beside it —
which is what `"../plugins/atp_template_plugin"` in `pipeline.json` resolves against):

```bash
atp_app config/pipeline.json      # prints lines reading "doubler: N -> M"
                                  # and reports "N x 3 = M (small|medium|large)" downstream
```

That is exactly what the `out-of-tree plugin` CI job does.

## License

The template is part of AniToolsPlatform and is covered by the Apache License 2.0: a copy of the text
sits in `LICENSE` beside it, copyright 2026 The AniToolsPlatform Authors. The
`// SPDX-License-Identifier: Apache-2.0` lines in `plugin.cpp` and `doubler_module.hpp` apply to the
code of the **template**, not to whatever grows out of it.

License your own plugin however you like, up to entirely closed: the SDK is permissive,
`atp::platform` is headers under Apache-2.0, and a work derived from them is under no obligation to
be open. Once you have copied the directory, replace `LICENSE` and the SPDX lines with your own — or
your code ends up published under someone else's copyright. The one thing Apache-2.0 asks in return
is that you keep the notices in whichever template files you leave as they are (section 4(c)).
