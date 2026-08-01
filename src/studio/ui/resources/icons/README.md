# Icons

One SVG per icon, loaded by `svg_icon` in `../../icons.cpp`. Replacing an icon means dropping in
another file with the same name — no C++ and no CMake change.

## Conventions

- `viewBox="0 0 24 24"` — the grid the whole family is built on.
- `fill="none"`, `stroke-width="1.8"`, `stroke-linecap="round"`, `stroke-linejoin="round"` — line
  art, not a filled shape. At a 16 px icon this weight lands just above one device pixel: thin
  enough to read as line art, thick enough to survive the scaling.
- `stroke="#000"`. The colour inside the file is ignored: the engine paints the artwork in the
  colour the palette gives text, so one file follows a light theme, a dark theme and any screen
  density.
- No partially transparent areas. The engine keeps the alpha of the artwork and replaces only the
  colour, so a half-transparent area stays half-transparent in every theme.
- Fills are allowed as detail (a dot, a small disc) and are written with the same `#000`; they get
  recoloured too. A shape with both a fill and the inherited stroke ends up as wide as the fill plus
  the pen, which is how the ellipsis on `save_as.svg` gets its weight.
