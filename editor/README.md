# hologram's editor

A page that runs `shaders/trace.glsl` in WebGL2 and flies a camera around a
scene while it renders.

```
build.bat
build\m7_room.exe --dump
python -m http.server 8731 --bind 127.0.0.1
```

Then <http://127.0.0.1:8731/editor/?s=m7_room>. Serve the **repository root**,
not this directory — the page reads the engine's files one level up.

`?s=` names any example that takes `--diff`: `m2_gpu`, `m3_mirrors`,
`m4_glass`, `m5_spectral`, `m6_polarization`, `m7_room`, `m8_furnace`,
`m9_spectrum`.

| | |
|---|---|
| `W` `A` `S` `D` | move |
| `Q` `E` | down / up |
| `shift` | faster |
| `Ctrl+S` / `Ctrl+Shift+S` / `Ctrl+O` | save / save as / open |
| `R` | back to the scene's own camera |
| `F` | look at the selected primitive |
| `P` | spectral on/off |
| `Ctrl+Z` / `Ctrl+Shift+Z` | undo / redo |
| click the view | mouse look; `Esc` releases |

## What it is

The engine's own tracer, not a preview of it. `tools/gldiff` proved the
arrangement — every browser ships a conformant GLSL ES 3.00 compiler and a
WebGL2 context, which is the dialect sokol's GLES3 backend feeds — and this is
that page with a camera and a frame loop around it.

Nothing is copied or generated for the editor's benefit. The tracer is read
from `shaders/`, and a scene is the `build/<name>_scene.json` that
`holo_oracle_dump()` writes on any `--dump`. Change the shader, press **reload
trace.glsl**, and you are looking at what is on disk.

There is no build step and no package manager, because the engine has no
package manager and this page is not the reason to give it one.

## The bench

Everything in the room is in one list — spheres, rects, dishes, and the two
scene-level groups, the floor and the sky, which are selected and edited like
anything else. Each row says what the thing *is* rather than what index it
has: `mirror`, `glass n=1.62 dispersive`, `polarizer`, `grating 1.2um`. `+`
adds, `×` deletes, **duplicate** copies, **look at** (or `F`) points the
camera at it, which is the difference between adding a panel and then finding
it.

The panels are generated from `core/schema.js` and nothing in `ui/` knows
what a sphere is. It knows `vec3`, `unit`, `angle`, `color`, `enum`, `vec4`
and `bool`, and it walks a table. A new material field on `HoloRect` is one
line in that table. The alternative — hand-written panels — is how an editor
falls a version behind its engine and starts lying about it.

Two things the tables carry that are worth more than the controls:

**The help text is `cpu_trace.h`'s.** Those comments are where the material
model is actually explained — that a rect's glass is a thin pane and a
sphere's is a volume, that Fresnel splits the transmit share by angle, that a
grating ignores the glass fields entirely — so they are the tooltips. Hover a
field label.

**Inert fields say so.** `ior` on something with no transmit, `retard` on a
polarizer, `grating_angle` with no period: the engine ignores these, so they
are greyed with the reason rather than hidden. A field that vanishes reads as
a missing feature; "ignored while transmit is 0" teaches the model. They
re-evaluate live — raise transmit off zero and `ior` un-greys in the same
breath, because a stale inert marker lies about exactly the thing the feature
exists to explain.

Undo is `vendor/history.js`, copied from magma-kit (see `vendor/PROVENANCE.md`).
Dragging a slider is **one** undo entry, not two hundred — that is what its
`beginStroke`/`commitStroke` pair is for, and it is the reason to take the
house module rather than write a fourth stack.

## The C it emits

**show C** turns the scene into a `HoloScene` designated-initializer block,
in the shape `examples/m7_room/main.c` is written in, plus the
`holo_camera_make` call for wherever you flew to. That is the whole path from
"tuned it until it looked right" to "the game does that now" — there is no
loader and there is not going to be one (`source/scene_json.h` has the
reasons).

Zero fields are omitted, because a designated initializer zero-fills what it
does not name and spelling them out buries the fields that matter. Floats get
the shortest text that reads back as the same float, which is why the output
says `0.85f` and not `0.850000024f`. The `f` suffix is not cosmetic: without
it every literal is a double, the initialiser promotes, and what the game
compiles is not bit-for-bit what the editor rendered.

The claim that nothing changes on the way round is checked, not asserted.
`roundtrip.c` holds a literal copied verbatim out of the panel, having opened
`m7_room` and changed nothing; compiling and packing it must reproduce the
block `--dump` wrote. Its header has the command.

```
round trip: 840 of 840 floats identical
EMITTER OK: byte-identical
```

## Saving

`Ctrl+S` saves, `Ctrl+Shift+S` saves to a new file, `Ctrl+O` opens one. The
first save asks where; every save after it is silent, because the browser
keeps the file handle. No desktop wrapper is involved — the File System
Access API does this from a plain page, and where it is missing (Firefox,
Safari) a save falls back to a download, which lands in the downloads folder
rather than where you asked. That difference is stated in the status line
rather than papered over.

Independently of either, **every edit writes a draft to localStorage**, so a
closed tab or an accidental reload does not lose work that was never saved
to a file. The draft is restored on load with a note saying when it was
taken and a button to discard it and go back to the dump.

### A save does not overwrite the dump

`build/<name>_scene.json` is what `--dump` produced, and `_params.bin` and
`_ref.bin` beside it are renders **of that scene**. The packer banner and the
oracle panel are both checked against them. Write an edited scene over it and
both would go on reporting confident verdicts about a room that is not the
one on screen — a red PACKER FAIL that is not a packer problem, and a diff
between two different scenes.

So a save goes wherever you point it, the dump stays the dump, and three
things keep the checks honest:

- The packer check runs against the scene **as fetched**, not the one being
  edited. It is a check on the packer and has to go on meaning that after an
  edit or a restored draft.
- A saved file records `"editor": { "edited": 1, "origin": ... }`, and a
  document carrying it — or one opened from a file — shows a warning saying
  its references belong to whatever was last dumped. Load one and the packer
  reports `UNCHECKED`, the oracle reports `no reference for this scene`, and
  neither invents a verdict.
- The oracle panel already says, separately, when the scene has been edited
  since load.

### The format is the C's, exactly

A saved file is `hologram/scene/1` — the same format `source/scene_json.c`
writes, down to key order, indentation and float spelling — so the editor
opens its own output the way it opens a dump.

That is checked rather than asserted: serialising each of the eight dumped
scenes and stripping the provenance block reproduces the C-written file
**byte for byte**. Getting there needed two details that do not change a
value and do change the text — `printf` pads an exponent to two digits
(`e-08`, where JavaScript writes `e-8`), and prints negative zero with its
sign, where `String(-0)` is `"0"`. Matching both is what lets a saved scene
be diffed against the dump it came from and show only the edits.

## The plan, and authoring walls

The tracer cannot draw a collision box. They are not optical — it has no
boxes and they reflect nothing — so in the first-person view a wall is
invisible and the only evidence of one is being stopped by it. That is a
poor way to author a room, and it is how crystal-mirror-maze's walls are
authored today: written as C, compiled, walked into.

The **plan** panel draws the room from above. Walls are axis-aligned boxes,
so a top-down projection loses only their height — which is exactly what the
row labels and the inspector carry. Drag a wall to move it, drag a corner of
the selected one to resize, wheel to zoom, drag the background to pan. `+`
adds one, `×` deletes, and `fit` frames everything.

Drawn faintly underneath are the scene's own panels and spheres, and that is
the point of the view rather than decoration. A collision world deliberately
does **not** match the geometry you can see — m7_room's mirrors sit at
x = ±4 while the walls that stop you are 0.3-thick slabs behind them — so
the mistakes worth catching are a wall that has drifted from the mirror it
backs, a doorway that quietly closed, a pane you can walk through. Nothing
here derives a wall from a panel; doing so would make them the same surface,
which the engine deliberately does not.

The walker's radius is drawn as a circle around your position, so a gap can
be judged by eye before you try to walk through it.

Height is the part a plan cannot show, so the list says it instead: a row
reads `2 x 0.1 wall`, or `curb 0.4 high`, or `overhang at 1.65`, and
anything you cannot simply walk into is drawn dashed and paler.

`show C` emits the world as a `HoloWalkWorld` block beside the scene, which
is how it gets into a game. `save walls` writes `hologram/walk/1` — the
format `walk_json.c` writes, minus the trace, because a trace is a record of
the C stepping a world and the editor cannot honestly produce one.

That emitter is checked the way the scene's is: `worldtrip.c` compiles a
literal copied out of the panel, hands it to `holo_walk_write_json`, and the
resulting `world` object is identical to the one in the dump.

### Editing walls withdraws the trace, not the check

A dumped trace is a record of the C stepping **that** world, so once the
walls move it no longer describes the room and replaying it would report a
broken twin when nothing about the arithmetic had changed. So the world is
kept twice — the one you edit and walk in, and the pristine one the check
replays — and once they differ the line says `world edited, trace withdrawn`
rather than restating a verdict it can no longer support.

The twin stays checked throughout, because `build/walk_selftest.json` is a
synthetic world no edit can reach. That is most of why it exists.

## Walking

`G` swaps the flying camera for a walking one, on the three examples that
have a walker. It is `holo_walk_step`, driven by the fixed-step accumulator
at the 120 Hz every walking example sets, with the eyes 1.55 above the feet
— so the room answers the question a flying camera cannot: not what a panel
looks like from over there, but whether a player can get there and what they
see when they do. A flying camera will happily stand inside a wall and show
you a sightline that does not exist.

`WASD` walks, `space` jumps, `G` goes back to flying.

The walls are **not** in the scene. `HoloWalkWorld` is a separate struct,
hand-authored boxes that deliberately differ from the panels you can see —
thicker, set back — because what stops a player and what reflects light are
not the same surface. So the walking examples now dump
`build/<name>_walk.json` beside the scene.

### This twin is checked harder than the others

`core/collision.js` is a copy of `collision.c`, and it is the one twin here
that no rendered image would catch drifting: a packer bug shows up as a
wrong picture, but a room you can walk through the wall of still renders
perfectly. So it is not checked by eye at all.

Every walk dump carries a **trace**: 660 scripted steps, recording what was
commanded and what came out. The editor replays the commands through its own
walk step and compares. The script is in the file, not shared knowledge, so
there is nothing to keep in sync but the arithmetic itself.

```
WALK OK  m7_room: 660/660 bit-identical   selftest: 570/570 bit-identical
```

Bit-identical, not within-a-tolerance — the bar is exactly zero. (Getting
there required one fix worth knowing about: `walk_json.c` writes the
shortest text that reads back as the same **float**, and `JSON.parse` returns
a **double**. Comparing a `Math.fround`ed result against the raw parse
disagrees in the last places on almost every row. Every number out of the
JSON goes back through `fround` before it is used.)

A room turns out not to exercise everything, though, and the gap is worth
naming. Every wall in one starts at the floor, so the vertical span test
never changes its answer; and a scripted walk only meets the walls it happens
to pass. Mutation-testing the check found both: changing `height` from 1.7 to
1.6 was not caught, and neither was deleting a wall the script never reaches.

So there is a second world in `build/walk_selftest.json`, built in
`walk_json.c` rather than dumped from a game, whose only purpose is to be
awkward — a plain wall, a curb low enough to jump over, and an overhang whose
underside clears the floor by less than a walker's height. Against that
world every mutation is caught:

| mutation | caught |
|---|---|
| `height` 1.7 → 1.6 | yes — walks under the overhang |
| curb raised out of jump range | yes |
| overhang lifted clear | yes |
| any wall deleted | yes |
| `radius` 0.3 → 0.301 | yes |
| `gravity` 20 → 20.01 | yes |
| `height` 1.7 → 1.8 | no, and correctly — a taller walker changes no outcome in that world |

## The sweep

Put a detector on the image, turn one knob, plot what comes out.

The **probe** is the yellow rectangle over the view — a photodiode, reading
its area as one number. The **sweep** steps one scalar field of the selected
primitive across a range, renders each step at the live camera, and plots
what the probe saw. Vectors and colours are not offered: a sweep needs one
number with an order to it, and "sweep the albedo" is three questions.

Nothing here knows Malus's law. Turn a polarizer through 180° with a
detector behind it and the law is what the curve does. That is the whole
point of building it this way — a second copy of the optics in JavaScript
(Fresnel, Malus, Cauchy, the grating equation) would be four more twins,
checked by nothing, answering questions about themselves rather than about
the tracer. A curve measured off the frame is a statement about what
hologram actually does.

**The one thing that has to be right:** the frame is sRGB-encoded, because
`oracle.c`'s `encode_u8` applies the transfer curve on the way out. The bytes
are not proportional to intensity, and averaging them measures nothing
physical — read a cos² straight off them and it comes out visibly wrong.
Every value is decoded back to linear first, then weighted Rec. 709.

### Does it actually measure the physics?

`m6_polarization`, probe on the two-polarizer path, sweeping the crossed
panel through 180°:

| θ | measured | cos²θ |
|---|---|---|
| 0° | 1.0000 | 1.0000 |
| 30° | 0.7436 | 0.7500 |
| 45° | 0.4929 | 0.5000 |
| 60° | 0.2442 | 0.2500 |
| 90° | 0.0000 | 0.0000 |
| 135° | 0.4929 | 0.5000 |

Worst deviation across the whole sweep: **0.0077**, which is sRGB
quantisation. And the three-polarizer paradox, against the unpolarized wall
seen through the gap between the panel groups:

| path | measured | expected |
|---|---|---|
| crossed pair, nothing between | 0.00000 | 0 |
| 0° / 45° / 90° | 0.12350 | 0.125 |

Those are the values `tests/test_polar.c` pins in closed form — Malus at five
angles, the three-polarizer eighth — recovered here from pixels, by a path
that shares no code with the test.

### Worth pointing it at

- **A polarizer's `filter_angle`** — Malus, and an extinction ratio, which is
  the number a real polarizer is judged by.
- **A dish's `curv_r`** on `m8_furnace`, probe at the focus — the intensity
  peaks where `test_geometry.c` says a paraboloid focuses, at R/2.
- **A sphere's `disperse`** with the probe on the spectrum — Cauchy B against
  how far the colours walk apart.
- **`grating_period`** with the probe on one order — the order sweeping
  across the wall as the grooves close up.

A sweep restores the value it moved, so the scene is exactly as you left it.
CSV goes to the clipboard.

## The oracle panel

**oracle** renders the tracer at the reference's resolution, compares it to
the CPU frame `--dump` wrote, and draws GPU, CPU and their difference at 8×
with `holo_oracle_diff`'s verdict. This is `tools/gldiff/gldiff.html`
absorbed: same WebGL2 arrangement, same reference files, but the browser is
already open, the scene is already loaded and the shader is already compiled.
It reproduces gldiff exactly — mean, max and outlier share — on all eight
dumped examples.

It diffs the scene **as dumped**, always: its camera, its spectral flag, its
resolution. Never the scene you have been editing. The reference is a CPU
render of the dumped scene, and there is no CPU tracer here to make another
one — writing a JavaScript `cpu_trace.c` would be a sixth statement of the
tracer, 578 lines of walk and stack and Fresnel, checked by nothing. So the
panel answers *"does trace.glsl agree with cpu_trace.c"*, which is a question
about the shader and not about your scene, and it says so on its face once
you have edited anything.

### oracle.c's mean is not gldiff's

Building this turned up a difference between the two tools that were supposed
to agree. `oracle.c` breaks out of the channel loop at the first channel off
by more than 8:

```c
sum += d;
if (d > max_diff) max_diff = d;
if (d > 8) { outliers++; break; }
```

so for an outlier pixel the later channels never reach `sum`, and `max_diff`
stops moving as well. `gldiff.html` sets a flag and keeps summing. They agree
exactly on a clean frame and drift apart in proportion to the outlier count —
which is to say they disagree precisely when a frame is failing and the
number matters most. On `m7_room`, from identical pixels:

| | mean | max |
|---|---|---|
| `oracle.c` | 0.0888 | 206 |
| `gldiff.html` | 0.1976 | 227 |

The outlier share, computed the same way in both, agrees to the digit.

This reaches the engine README's pass table, whose **D3D11 column is
oracle.c's mean and whose WebGL2 column is gldiff's**. They are not the same
statistic, so reading across a row overstates how far WebGL2 drifts. Measured
one way throughout, the two columns very nearly coincide:

| example | D3D11 (published) | WebGL2 frame, oracle.c's mean | WebGL2 (published, gldiff's mean) |
|---|---|---|---|
| `m4_glass` | 0.1161 | 0.1161 | 0.1603 |
| `m5_spectral` | 0.1183 | 0.1183 | 0.2512 |
| `m6_polarization` | 0.0608 | 0.0608 | 0.0657 |
| `m8_furnace` | 0.0185 | 0.0185 | 0.0266 |

So the GLSL tracer agrees with the oracle about as closely as the HLSL one
does, and most of the table's apparent WebGL2 divergence is the statistic
changing between columns — which is also why the README's own advice to read
the outlier percentages rather than the means is the right advice.

The panel reports oracle.c's number as the verdict, because oracle.c is what
gates every example, and prints gldiff's beside it whenever the two differ.
Nothing in the engine has been changed to force the question: whether the
`break` should truncate the sum is an engine decision, and it would move
published numbers.

## The budget panel

24 rects, 8 spheres, 4 dishes — the fixed-size arrays in `cpu_trace.h`.
Nothing raises an error when a scene passes one; the primitive is simply not
traced. crystal-mirror-maze's first hall is at 24 of 24, which is most of why
this panel exists.

The GPU grating limit is the one worth watching. Gratings live in two *scalar*
uniform slots rather than an array, because fxc corrupts a dynamically indexed
one (`gpu_scene.h` has the story). A third grating renders **matte black on
the GPU while the CPU oracle renders it correctly** — a scene that looks right
in a CPU render and is wrong in the game. The panel says so in red.

## The packer check

`core/scene.js` packs a scene into the uniform block, which makes it a **fifth
statement** of a layout already written four times: `HoloGpuScene`, and
`trace.hlsl` / `trace.glsl` / `trace.metal`. `gpu_scene.h` warns about exactly
this — the struct and the shader "must change together". A fifth copy that
nothing checks would be a liability, so two things check it, neither of them
discipline:

**The layout table checks itself.** `LAYOUT` carries each field's slot number
as the shader spells it, and a running total. Update one and not the other and
the page throws by name instead of rendering something plausible.

**Every example is a conformance case.** `build/<name>_params.bin` from a
`--dump` *is* the block C packed. The page packs the same scene in JavaScript
and compares float by float, and reports it in the banner.

Across all eight dumped examples every geometry field comes back
bit-identical — the camera basis, the rect solve vectors, the corners, the
materials. The only floats that differ at all are the computed ones, where C
uses `expf` and `cosf` and JavaScript uses `Math.exp` and `Math.cos`:

| field | why it differs | worst |
|---|---|---|
| `spectral_lw[]` | CIE fits, `Math.exp` vs `expf` | 1.19e-7 |
| `rect_filter[]` | filter axis, `cos(π/2)` is -4.37e-8 in C and -7.32e-8 here | 5.96e-8 |
| `grat*_groove_idx` | groove direction, same `cos(π/2)` | 2.95e-8 |

The bar is `|a-b| <= 1e-6 + 1e-6*|a|`. Both terms are needed and
`core/scene.js` explains why at length: a pure *relative* tolerance calls a
rounding difference on a near-zero CIE weight a 3.7e-5 error, and a pure *ulp*
distance calls the two spellings of `cos(π/2)` six million floats apart. Both
are wrong about the same non-event. Measured headroom is about 8×, while a
field written to the wrong slot lands off by its own magnitude — five to seven
orders of magnitude above the bar. Verified: shifting `rect_solve_v` by one
slot, swapping `cam_right` with `cam_up`, or moving a radius by 1e-5 all turn
the banner red.

## Layout

```
core/     pure: no DOM, no WebGL
  linalg.js     source/linalg.c + holo_rect_basis + holo_camera_make
  spectrum.js   holo_lambda + holo_spectral_weight
  caps.js       the hard limits, and what happens when a scene passes one
  scene.js      holo_gpu_scene_fill, the layout table, the conformance check
  schema.js     what a primitive's fields are, and what they mean
  emit.js       scene -> HoloScene C
  save.js       scene -> hologram/scene/1 JSON, and drafts
  oracle.js     holo_oracle_diff's arithmetic
  sweep.js      the probe, and what a sweep measures
  collision.js  holo_walk_step, and the trace that checks it
  timestep.js   the fixed-step accumulator
ui/
  view.js       WebGL2: compile, upload, draw, read back
  camera.js     free flight
  inspector.js  controls generated from schema.js
  bench.js      the list, selection, add/delete, undo, the C panel
  oraclepanel.js  GPU, CPU, difference
  sweeppanel.js   the probe overlay, the sweep, the plot
  planview.js   the room from above, and wall authoring
  files.js      save, save as, open, and the download fallback
  main.js       loading, the frame loop, input
vendor/
  history.js    magma-kit's undo stack, copied — see PROVENANCE.md
roundtrip.c     proves emit.js round-trips through the compiler
worldtrip.c     the same, for the walk world
```

`core/` is where correctness lives and `ui/` is the DOM — the same split
sprite-forge, deck-press and gatefold use.

Everything in `core/` puts each arithmetic step through `Math.fround`. That is
not ceremony: hologram computes in `float` deliberately, so the CPU oracle
works in the precision the GPU will, and rounding only at the end gives a
different answer from rounding at every step.

## Not here yet

**No dragging in the view.** Placement is numeric. A gizmo needs the tracer
to say which primitive a pixel belongs to, which the tracer does not report
today — an object-id pass would be a real change to the shader, not an
editor feature.

**No diff of an edited scene.** See the oracle panel above: the CPU reference
belongs to the dumped scene, and making another needs `cpu_trace.c`, which is
C and is staying C.

**No bench.** `tools/bench` prices a scene by panel count and is exactly what
you want beside a budget meter, but it times the GPU from a native process.
Running it needs somewhere to spawn one, which a page does not have.

**A sweep in a background tab crawls.** The loop yields with `setTimeout` so
the progress line paints, and a hidden tab throttles those to about one a
second. Correct behaviour — a page nobody is looking at should not hold the
GPU — but it means a sweep wants the window in front of you. Forty-eight
steps takes a second or two when it is.

**Moving a panel still does not move its wall.** Both are editable now, and
independently, which is faithful to the engine — but it means a mirror and
the slab behind it can drift apart, and only the plan view will show you.

**No walking at all outside the three examples that have a walker.** m2
through m6 have no `HoloWalkWorld` to dump, so `G` does nothing there and the
control is hidden.
