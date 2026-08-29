# Icons

One SVG per icon, loaded by `svg_icon` in `../../kit/icons.cpp`. Replacing an icon means dropping in
another file with the same name — no C++ and no CMake change.

## Conventions

- `viewBox="0 0 24 24"` — the grid the whole family is built on.
- `fill="none"`, `stroke-width="2"`, `stroke-linecap="round"`, `stroke-linejoin="round"` — line art,
  not a filled shape.
- **Coordinates are whole numbers, and the pen is even.** Both halves matter and only together: a
  stroke of 2 centred on an integer has its two edges on integers as well, so at the 24 px a toolbar
  asks for the line covers whole pixels, and it still does at 150 % (coordinate ×1.5 lands on a half,
  pen 3, edges back on integers) and at 200 %. The family used to be drawn at 1.8 on coordinates
  like `2.5` and `6.6`, and every straight edge then landed across two pixel columns at partial
  coverage — which is what "the icons look blurry" was. Curves are antialiased whatever you do; it is
  the straight edges that carry the impression.
- **The drawing fills the box `3 … 21` on its longer side**, which with the pen is ink from 2 to 22 —
  five sixths of the grid. This is the other half of "one style": before it, `run.svg` was drawn 10.6
  units wide next to an 18-unit `save.svg`, and the two sat side by side on the toolbar.
  `tests/ui/icons_tests.cpp` measures it — what the file paints, not what its viewBox declares — so a
  replacement that ignores the box fails the suite rather than the eye.
- `stroke="#000"`. The colour inside the file is ignored: the engine paints the artwork in the
  colour the palette gives text, so one file follows a light theme, a dark theme and any screen
  density.
- No partially transparent areas. The engine keeps the alpha of the artwork and replaces only the
  colour, so a half-transparent area stays half-transparent in every theme.
- Fills are allowed as detail (a dot, a small disc) and are written with the same `#000`; they get
  recoloured too. A detail that is a fill carries `stroke="none"` and its own radius rather than
  riding on the group's pen, so that its size is one number in the file instead of two added
  together — which is how the ellipsis on `save_as.svg` and the star on `lua.svg` are drawn.

## The two deliberate exceptions

`stop.svg` is drawn in a 16-unit square rather than an 18-unit one, and that is not a slip. A square
covers its own bounding box; a triangle covers half of it and an outline much less, so a stop at the
family's size reads heavier than the run triangle it stands next to. The measured band in the test is
wide enough to hold it and far too narrow to hold the sizes that were there before.

`python.svg` carries a 1.6 pen. Its mark is two interlocking bodies where the rest of the family is
three or four strokes, so at the family's weight it carries visibly more ink than its neighbours —
and pulling the eye across the canvas to whichever node happens to be a Python one is the one thing a
label on a node must not do. That is the same reason the mark is an outline rather than the filled
silhouette the logo usually is.

## Holes are holes, not white

The engine keeps the **alpha** of the artwork and replaces only the colour, so a detail painted white
does not stay white — it comes back in the ink like everything else, and disappears into the shape
around it. A gap has to be a real gap: leave it unpainted, or cut it out of its own path with
`fill-rule="evenodd"`.

## The marks a canvas node wears

`module.svg`, `group.svg` and `script.svg` are also the marks beside a node's name on the canvas, and
a language of `studio/languages.hpp` takes its own file named after the language's id — `python.svg`,
`lua.svg`. That name is the whole binding: `icons::script_artwork` looks the file up, so giving a new
language a mark means dropping in `<id>.svg` and nothing else, and a language with no file of its own
falls back to `script.svg`. On the canvas the artwork is tinted with a colour of the canvas scheme
rather than the palette's text colour, which is a second reason the files must carry no colour of
their own.

**On the canvas the margins a file leaves around its drawing do not matter.** Each mark is measured
once — what it paints, not what its viewBox declares — and drawn so that measurement fills the
square, aspect kept. So a replacement need not match this family's padding; it only has to be drawn
inside the 24×24 grid. In the menus and the panels the file is used as it is, which is the older
behaviour and why the box above is a rule there and not here.
