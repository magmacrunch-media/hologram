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
  oracle.js     holo_oracle_diff's arithmetic
ui/
  view.js       WebGL2: compile, upload, draw, read back
  camera.js     free flight
  inspector.js  controls generated from schema.js
  bench.js      the list, selection, add/delete, undo, the C panel
  oraclepanel.js  GPU, CPU, difference
  main.js       loading, the frame loop, input
vendor/
  history.js    magma-kit's undo stack, copied — see PROVENANCE.md
roundtrip.c     proves emit.js round-trips through the compiler
```

`core/` is where correctness lives and `ui/` is the DOM — the same split
sprite-forge, deck-press and gatefold use.

Everything in `core/` puts each arithmetic step through `Math.fround`. That is
not ceremony: hologram computes in `float` deliberately, so the CPU oracle
works in the precision the GPU will, and rounding only at the end gives a
different answer from rounding at every step.

## Not here yet

**No saving.** Edits live in the page and leave as C through the panel;
reloading loses them. A browser cannot write `build/<name>_scene.json` back,
and the "edited" marker in the header is honest about what that means. This
is the one place the no-desktop-wrapper choice actually costs something.

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

**No walking.** Flying has no capsule, no floor and no walls. Walking the
scene the way the game will needs `collision.c` and `timestep.c` ported as
twins, and a twin nothing checks is the thing this page is otherwise careful
to avoid — so it waits until it is worth the check.
