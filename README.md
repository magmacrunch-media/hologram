# hologram

An optics-first 3D game engine by [magmacrunch media](https://magmacrunch.com):
a real-time spectral ray tracer where light obeys physics. Mirrors reflect
recursively, prisms cast real spectra, polarizers extinguish at the angles
Malus says they should, paraboloids focus at R/2, and diffraction gratings
throw their orders by the conical grating equation. The renderer traces
wavelengths rather than RGB triples, carries a polarization state on every
ray, and meets every surface in closed form.

Named after the Dag Henderson track "hologram of a dream", published by
magmacrunch music.

Current version: **0.2.0** in progress; **v0.1.0** is the first tagged
release (see [CHANGELOG.md](CHANGELOG.md)). The full
reference lives in the [wiki](https://github.com/magmacrunch-media/hologram/wiki).

![Two ruled panels diffracting sunlight into their orders](docs/images/m9-gratings.png)

## What it does that other engines don't

Every effect below is computed from the physics rather than approximated by a
shader trick, and every one is pinned by a host test against its closed-form
answer.

| | |
|---|---|
| **Recursive mirrors.** Facing mirrors produce a true infinite corridor to any depth, at any viewing angle. Screen-space reflection structurally cannot draw this frame; it can only reflect what is already on screen. | ![](docs/images/m3-mirrors.png) |
| **Glass by the Fresnel equations:** the real pair, s and p computed separately, not Schlick's fit. Refraction, total internal reflection, and reflectance that rises toward grazing incidence because physics says so rather than because a parameter does. | ![](docs/images/m4-glass.png) |
| **Spectral light.** Twelve wavelengths per pixel, each refracting at its own Cauchy `n(λ)`, folded to sRGB through the CIE 1931 colour matching functions. Crown glass fringes gently; flint tears the same edge into a spectrum five times wider, which is why camera lenses pair the two. | ![](docs/images/m5-spectral.png) |
| **Polarization.** Every ray carries a Stokes vector; every interface applies a Mueller matrix. Crossed polarizers go black, a third at 45° between them brings back exactly one eighth, and a waveplate writes interference colour because its retardance runs as 1/λ. | ![](docs/images/m6-polarization.png) |
| **Curved mirrors as optics quotes them:** apex, axis, vertex radius of curvature, conic constant, rim. Stand at a paraboloid's focus and every zone of the dish reflects your eye into the sun, so the whole aperture flashes. A solar furnace, from the inside. | ![](docs/images/m8-furnace.png) |
| **Diffraction gratings** in the conical (off-plane) vector form. The groove component of the direction is conserved, the dispersion component picks up `mλ/d`, and `m = 0` falls out as exact specular. Orders sweep across a ruled panel as you walk, the way a CD tilts its colours. | ![](docs/images/m9-gratings.png) |

## Why a ray tracer

The games this engine exists for (starting with Crystal Mirror Maze) are built
entirely from analytic primitives: planes, rectangles, spheres, conics. They
are rendered at low resolution by design. Closed-form intersections at 640×480
are exactly the workload a fullscreen-shader ray tracer can afford in real
time, and a tracer is the only renderer in which curved mirrors, refraction,
dispersion and polarization are *correct* rather than faked.

## Building

Requires Visual Studio (MSVC). No other dependencies; sokol is vendored.

```
build.bat          # builds every example into build\
build.bat test     # builds and runs the host tests
```

Run the examples from the repository root, since they read
`shaders\trace.hlsl` at startup. You can edit the tracer and relaunch without
recompiling.

## The examples

Each milestone left a runnable demo behind. Every GPU example accepts
`--diff`, which renders the same frame through the CPU tracer and compares
(see [The oracle](#the-oracle) below); the exit code is the verdict. They also
accept `--dump`, which writes that comparison's inputs out for `tools/gldiff`.

| | |
|---|---|
| `m0_window` | A window, a fullscreen quad, uniforms arriving every frame. |
| `m1_cpu` | The CPU tracer's first picture, written to `build\m1_cpu.ppm`. |
| `m2_gpu` | The same scene traced on the GPU, held to the oracle. |
| `m3_mirrors` | Facing mirror walls, a chrome sphere, a polished floor, all recursing. |
| `m4_glass` | A ball lens holding the checker floor upside down inside it. |
| `m5_spectral` | Crown and flint glass in front of one white stripe. |
| `m6_polarization` | The three-polarizer paradox, and a waveplate's interference colour. |
| `m7_room` | **The vertical slice:** a room of mirrors, glass, a polarizer and a flint ball that you walk through in first person. |
| `m8_furnace` | A paraboloid with its focus at eye height on the path. Walk into it. |
| `m9_spectrum` | Two ruled gratings throwing the sun's orders back at you. |

`m7_room`, `m8_furnace` and `m9_spectrum` are interactive: click to capture the
mouse, `WASD` to walk, `Space` to jump, `T` to toggle spectral tracing, `Escape`
to release the mouse.

![The walkable room](docs/images/m7-room.png)

## Architecture

- **C99 engine, one tracer in three dialects** (HLSL for D3D11, GLSL for GL
  and GLES3/WebGL2, MSL for Metal), on [sokol](https://github.com/floooh/sokol)
  (vendored, zlib licence) for the window, GPU device and swapchain. Windows
  ships and Linux is proven -- all eight examples pass the oracle natively on
  GL as well as under D3D11 -- while the Metal dialect is written,
  type-checked, and has not yet met a Metal device.
- **The tracing runs on the GPU** as a fullscreen-quad fragment shader over a
  scene of analytic primitives delivered in one uniform block.
- **Fixed-timestep simulation** (`source/timestep.c`, ported verbatim from
  [magnolia](https://github.com/magmacrunch-media/magnolia)).

Games compile the engine's sources directly, magnolia-style; there is no
library build and no package registry. A game is three callbacks and a scene.

### The oracle

`source/cpu_trace.c` is a complete second tracer in plain C, and the three
shaders under `shaders/` are its statement-for-statement twins. The C is
allowed to be slow and obliged to be right: every optical law lands there
first, held by host tests to closed-form answers, and then
`holo_oracle_diff()` reads the presented GPU frame back and compares it to a
CPU render of the same camera.

That diff is the engine's central correctness mechanism. A change that lands
in one tracer and not the others stops being a mystery and becomes a failing
exit code. The bar is a mean error under 1/255 with under 0.75% of pixels off
by more than 8/255.

Each cell below is that pair: mean error in 1/255 levels, then the share of
pixels off by more than 8/255. Every one passes.

| example | what it puts under the light | HLSL, D3D11 | GLSL, Linux GL | GLSL, WebGL2 |
|---|---|---|---|---|
| `m2_gpu` | spheres, checker floor, the RGB walk | 0.1030 · 0.001% | 0.0001 · 0.000% | 0.1033 · 0.001% |
| `m3_mirrors` | facing mirrors recursing | 0.0480 · 0.001% | 0.0003 · 0.001% | 0.0482 · 0.001% |
| `m4_glass` | Fresnel, refraction, total internal reflection | 0.1161 · 0.163% | 0.0201 · 0.123% | 0.1603 · 0.163% |
| `m5_spectral` | twelve wavelengths, Cauchy dispersion | 0.1183 · 0.503% | 0.0802 · 0.383% | 0.2512 · 0.498% |
| `m6_polarization` | Stokes vectors, Mueller matrices, a waveplate | 0.0608 · 0.015% | 0.0015 · 0.009% | 0.0657 · 0.015% |
| `m7_room` | all of the above at once, spectrally | 0.0834 · 0.230% | 0.0400 · 0.156% | 0.1976 · 0.236% |
| `m8_furnace` | conic dishes, focusing at R/2 | 0.0185 · 0.022% | 0.0015 · 0.010% | 0.0266 · 0.022% |
| `m9_spectrum` | gratings, conical orders | 0.0949 · 0.025% | 0.0026 · 0.010% | 0.0973 · 0.025% |

The outlier percentages track each other across all three, which is the
thing worth reading: it says every tracer takes the same branches and culls
the same rays. The Linux column was measured under Mesa's software
rasteriser, which does the arithmetic the way the CPU oracle does -- so it
shows what the agreement looks like with the GPU's fast-math divergence
taken away, and the answer is four decimal places. The other two columns
are that same agreement plus each driver's own liberties.

**What has actually been run.** The table above is the whole of it; the rest
of the porting work is written but unproven, and should be read that way.

| path | state |
|---|---|
| HLSL tracer, D3D11 readback | green, eight of eight |
| GLSL tracer | green, eight of eight, natively on Linux GL and through `tools/gldiff` |
| GL readback (`glReadPixels`) | green -- it is what the native Linux `--diff` reads |
| MSL tracer | type-checks under `tools/metalcheck`; no Metal device has seen it |
| Metal readback | type-checks as Objective-C under `tools/metalcheck`; never built for a real SDK |
| `build.sh` on Linux | builds and runs; tests and all eight `--diff` green |
| Windows binary under Wine | green 8/8, once Microsoft's `d3dcompiler_47.dll` sits beside the exe -- Wine's own HLSL compiler silently miscompiles the tracer |
| `build.sh` on macOS | never executed |

The catch is that a readback needs the backend it runs on, and only the D3D11
one has ever run -- so on Windows the GL tracer cannot be held to the oracle
by `--diff` at all. `tools/gldiff` closes that gap.
Every example that accepts `--diff` also accepts `--dump`, which writes the
uniform block exactly as the shader receives it alongside the CPU's frame;
`tools/gldiff/gldiff.html` then renders `shaders/trace.glsl` in a WebGL2
context and compares, using the same arithmetic and the same bars, and draws
the two frames and their difference side by side.

```
build\m7_room.exe --dump
python -m http.server 8731 --bind 127.0.0.1
```

then open `http://127.0.0.1:8731/tools/gldiff/gldiff.html?s=m7_room`. It is
not a substitute for running `--diff` natively on the backend you ship, which
also exercises sokol's plumbing and the real driver -- but it catches the
tracer's own bugs, which is most of them.

The Metal side has no such luxury: off macOS neither the tracer nor the frame
readback can be compiled, let alone run. `tools/metalcheck` gets what it can.
MSL is a C++14 dialect and the readback is Objective-C, so with stand-ins for
the headers a host compiler will parse and type-check both -- the shader with
any C++ compiler, the readback with clang, which has the ARC support gcc
lacks:

```
python tools/metalcheck/metalcheck.py
```

That validates names, arities, types, selectors and bridge casts -- it will
catch a `params` argument dropped from one of the six functions that take
one, or a misspelled Metal selector, which are the mistakes this port is
likeliest to make. It says nothing about whether the selectors match Apple's
real ones, whether the Metal attributes are right, whether the two stages
link, or whether any of it renders. Those need a Mac, and until one has run
`--diff` the whole Metal path should be read as unproven.

### What a panel costs

The oracle says whether the tracer is right. `tools/bench` says what it
costs, which is the question a mirror maze raises: `nearest_hit` scans the
panels linearly, once per ray per bounce, and a full mirror keeps every ray
alive to the bounce cap. Panel count is a frame-time budget, not a capacity.

```
buildench.exe
```

It sweeps 1 up to `HOLO_MAX_RECTS` in the same window, timing the GPU rather
than the frame. That distinction matters: with vsync on every number is the
refresh interval, and with it off sokol presents without waiting, so the CPU
runs ahead and the frame clock measures the loop. On D3D11 bench uses
timestamp queries, and on GL `GL_TIME_ELAPSED` queries, which time the pass
and nothing else; on any other
backends it falls back to the frame clock and says so in its own output.

Raising `HOLO_MAX_RECTS` (and, by hand, the slot map the GLSL and MSL tracers
carry) lets it sweep further. At 640x480, spectral, on one desktop GPU:

| panels | 1 | 8 | 24 | 40 | 56 | 64 |
|---|---|---|---|---|---|---|
| GPU ms | 0.21 | 0.64 | 2.17 | 4.89 | 8.68 | 10.67 |

Fifty times the cost from one panel to sixty-four, which is what a linear
scan through a mirrored room looks like.

It is also a shader A/B rig -- `--shader PATH` runs the sweep against any
tracer file -- and it has already overruled intuition once. Hoisting the rect
basis out of the intersection was a clear win; *also* shipping the
precomputed normal looks like the same kind of win and is 1.35x SLOWER at 64
panels, on D3D11 and WebGL2 alike, because the extra indexed constant fetch
costs more than the cross and normalize it saves. That is why the tracers
derive the normal from the solve vectors rather than being handed it.

### Engine modules

Pure arithmetic is split from platform calls so the arithmetic is host
testable, the discipline magnolia's `timestep.c` was extracted for.

| Module | |
|---|---|
| `linalg.c` | Vectors, and the optics that is vector arithmetic: reflection, Snell refraction, the Fresnel equations, the grating equation. |
| `polar.c` | Stokes rows, Mueller matrices, Fresnel amplitudes with the TIR phase. |
| `spectrum.c` | Wavelength samples, Cauchy dispersion, CIE colour matching. |
| `geometry.c` | Rays against spheres, planes, rectangles and conic dishes. |
| `camera.c` | The camera as a ray generator. |
| `cpu_trace.c` | The reference tracer, the oracle. |
| `gpu_scene.c` | The scene as the shader's uniform block. |
| `collision.c` | Capsule-vs-walls walking with gravity. |
| `timestep.c` | Fixed-step accumulator (ported from magnolia). |
| `display.c` | The only file that talks to sokol: window, device, quad, uniforms, frame readback. |
| `input.c` | Keys held and mouse look, folded per frame. |
| `oracle.c` | The GPU-vs-CPU frame diff. |

## Testing

```
build.bat test
```

**438 checks across 9 suites**, each test a standalone binary. They assert
physics, not pixels: Snell's angles into n=1.5 glass, the 41.81° critical
angle, 4% reflectance at normal incidence, a vanishing p-component at
Brewster's angle, Malus's law at five angles, the three-polarizer paradox to
the exact eighth, BK7's Abbe number computing to its catalogue 64, a
paraboloid focusing every zone at R/2, an ellipsoid imaging focus onto focus,
Littrow retroreflection, and the conical invariant.

The one exception is `test_gpu_layout.c`, which asserts bookkeeping rather
than optics: the GLSL tracer reads the scene by slot number out of one `vec4`
array, and that slot map is a hand-written copy of `HoloGpuScene`'s layout
which no compiler checks. The test parses the shader and holds every slot to
`offsetof`, so reordering the struct fails a test instead of quietly making
the GPU read the camera out of the sun.

## Constraints worth knowing

The GPU walk is **chain and fork**: each ray walks as a chain, every surface
continuing in place and forking *at most one* side branch onto the stack. One
push per interaction is a hard ceiling, because fxc (D3D11's shader compiler)
silently corrupts the walk's stack arrays when a single branch pushes twice.
Per-grating data also cannot live in dynamically indexed constant-buffer
arrays for the same reason, so the grating constants sit in scalar uniform
slots instead. The CPU tracer mirrors the structure exactly, which keeps the
oracle diff meaningful. See
[Shader constraints](https://github.com/magmacrunch-media/hologram/wiki/Shader-constraints)
in the wiki for the full account.

A corollary that matters for Wine and Proton: hologram compiles its HLSL at
**runtime**, and off Windows the `D3DCompile` it calls is Wine's own HLSL
compiler, not Microsoft's. Wine's compiler takes the 700-line tracer without
a word of complaint and miscompiles it -- every pixel wrong, no error
anywhere. With Microsoft's `d3dcompiler_47.dll` placed beside the exe (and,
under bare Wine, `WINEDLLOVERRIDES="d3dcompiler_47=n"`), the same binary
passes all eight oracle diffs through the translation stack. A Wine or
Proton build of a hologram game must ship that DLL -- it is a Microsoft
redistributable -- or precompile its shaders.

## The editor

```
build.bat
build\m7_room.exe --dump
python -m http.server 8731 --bind 127.0.0.1
```

Then <http://127.0.0.1:8731/editor/?s=m7_room>.

`editor/` runs `shaders/trace.glsl` in WebGL2 and flies a camera around a
scene while it renders -- the engine's own tracer, not a preview of it, on the
arrangement `tools/gldiff` proved. It reads the engine's files in place: the
shader from `shaders/`, the scene from the `build/<name>_scene.json` that any
example writes with `--dump`. Every example opens in it.

The room is a list you can edit: select a panel, tune its material against the
real tracer, and take the result away as the `HoloScene` literal that built
it. The inspector is generated from a field table rather than hand-written,
and carries `cpu_trace.h`'s own comments as its help; fields the engine will
ignore given the rest of the primitive say so instead of sitting there
looking live.

It shows the budget against the caps while you look at the room -- 24 rects, 8
spheres, 4 dishes, and the two GPU grating slots whose third entry renders
matte black on the GPU while the CPU oracle renders it correctly. Adding is
refused at the cap rather than allowed and silently dropped by the tracer.

What it emits is checked, not asserted: `editor/roundtrip.c` compiles a
literal copied out of the editor and packs it, and gets the block `--dump`
wrote back byte for byte.

It also holds the tracer to the oracle in place -- GPU frame, CPU reference
and their difference, on oracle.c's bars -- reproducing `tools/gldiff`
exactly on all eight dumped examples without a second page or a second
server. See `editor/README.md` for what that panel does and does not
claim, and for a difference between oracle.c's mean and gldiff's that the
table below inherits.

Scenes edited in it are saved with Ctrl+S -- to a file you pick, never over
the dump, whose params.bin and ref.bin belong to the scene as it was and
are what the editor's own checks are held to. The saved format is this
one, byte for byte, so the editor opens its own output the way it opens a
dump. No desktop wrapper: a page can write a file the person chose, and
keep the handle.

On the examples that have a walker it also walks: holo_walk_step at the
same 120 Hz, with the eyes where a player's are, so the room can answer
whether a sightline is one a player can actually stand in. The walls come
from a new `build/<name>_walk.json`, since HoloWalkWorld is not part of a
HoloScene, and that dump carries a trace of its own walk which the editor
replays to hold its copy of the step to this one -- bit-identical, on a
world built to be awkward as well as on the game's.

And it measures. Put the probe rectangle on the image, sweep one field of
one primitive, and read the curve off the render: a polarizer's angle
gives Malus and an extinction ratio, a dish's vertex radius gives the
focus, a Cauchy coefficient gives the spread. Nothing in the editor knows
those laws -- they are what the tracer does, measured. Sweeping
`m6_polarization` reproduces cos^2 to 0.0077 and the three-polarizer
eighth to 0.12350, which are the numbers `tests/test_polar.c` holds in
closed form, arrived at from pixels by a path sharing no code with it.

Packing a scene into the uniform block is a **fifth statement** of a layout
already written four times (`HoloGpuScene` and the three shader dialects), so
it is held the way the others are. `build/<name>_params.bin` from a `--dump`
*is* the block C packed; the editor packs the same scene in JavaScript and
compares float by float, reporting the result in the page. Across the eight
dumped examples every geometry field comes back bit-identical, and the only
floats that differ at all are the ones C computes with `expf` and `cosf` and
JavaScript with `Math.exp` and `Math.cos`: the CIE weights and the filter and
groove axes, worst case 5.96e-8. A field written to the wrong slot is wrong by
its own magnitude, five orders of magnitude above that bar.

No build step, no package manager, no dependency -- it is a page, like
`tools/gldiff` is a page.

## Not in the engine

No ECS, no general physics engine, no mesh import, no skinned animation, no
GUI toolkit. Neither target game needs any of them, and each would cost more
than it returns. Scenes are built in code from primitives; collision is a
capsule against axis-aligned walls.

Scenes are still built in code. `holo_scene_write_json` writes one out, for
the editor and for anything else that wants to read a scene, but there is no
reader: a parser in C99 would be a runtime dependency, a new way for a shipped
game to fail, and a second way to build a scene competing with the first.

## Dependencies

[sokol](https://github.com/floooh/sokol) (`sokol_app`, `sokol_gfx`,
`sokol_glue`, `sokol_log`) by Andre Weissflog, vendored under
`external/sokol/` and used under the zlib licence.

## License

[PolyForm Noncommercial 1.0.0](LICENSE): read it, learn from it, build on it,
play with it. Any noncommercial purpose is permitted, and commercial use is
reserved to magmacrunch media (ask about a commercial licence). See
[NOTICE](NOTICE) for the exact boundary. This differs deliberately from
magmacrunch's Apache-2.0 2D engines: those are infrastructure for anyone,
while hologram is the optics engine under magmacrunch's own games. Vendored
sokol headers keep their zlib licence (`external/sokol/LICENSE`).
