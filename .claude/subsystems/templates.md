# Out-of-tree template projects

`templates/plugin/` is a plugin project **outside** this build — it is not `add_subdirectory`'d and
reaches the SDK only through `find_package`. It doubles as the fixture of the `out-of-tree plugin` CI
job, the one place where both ends of a connection come from different libraries. It names the ABI it
targets (`atp_require_plugin_abi(1)`), so **bumping `plugin_abi` means editing that file too** or the
job stops configuring — which is the intended feedback, not breakage. That feedback only exists while
CI runs: the templates are not `add_subdirectory`'d, so nothing in a local build ever compiles them,
and a change to what a module writes against — the shape of `atp::ports`, how sections are reached —
goes unnoticed here until someone configures the project out of tree by hand.

`templates/plugin_rust/` is the second out-of-tree project and the fixture of the `rust plugin` CI job: a
`cdylib` built by `cargo build` alone, with no dependencies and no CMake, whose `src/abi.rs` is a
**hand-written mirror** of `include/atp/plugin_c.h`. Two consequences. Changing the layout of anything in
that header — reordering or retyping a field, not just adding one — silently breaks every foreign mirror,
which is why `tests/platform/plugin_c_layout_tests.cpp` pins the sizes and offsets with `static_assert`;
update both together, and bump `ATP_C_ABI` if the meaning changed. And cargo cannot drop the `lib` prefix
from a `cdylib`, so the job renames the artifact to the prefix-free name the platform expects — the
template's README says the same. The `c header is C` job compiles `plugin_c.h` as strict C99 and C11 under
gcc and clang, which is the only place that would notice it drifting into C++.

`atp_module_desc` grew two fields — `config_fields` and `config_field_count` — so the hand-written Rust
mirror in `plugin_rust/src/abi.rs` carries them, and `AtpConfigFieldDesc` beside them: **one more layout
to keep in step with the header by hand**, with the same caveat as the rest of that file — an added field
is caught by `struct_size`, a reordered one by nothing.

**Each script template now shows both ways a config can reach a module**, and which one a copying author
wants is the point of having both. `plugin_rust` is where they sit side by side: `rust_averager` leaves
`config_fields` null and walks the document itself with fallbacks at every step, which is what a config
whose form the platform cannot express needs; `rust_gate` declares its fields and then reads them with
no fallback and no form check, because the host put every declared key in the tree before `create` ran.
`plugin_python/averager.py` and `plugin_lua/scale.lua` declare, and say in their doc comments when not
to. `plugin/doubler_module.hpp` declared from the start.
