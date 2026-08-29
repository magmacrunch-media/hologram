# Changelog

All notable changes to the hologram engine are documented here.

## v0.1.0 (unreleased)

The founding release: repository skeleton, the M0 window, and the seams every
later milestone builds on.

### Engine core

- **Vector optics** — `source/linalg.c`: vectors plus the optics that is just
  vector arithmetic (mirror reflection, Snell refraction with TIR reported to
  the caller), host-tested against hand-worked angles down to the critical
  angle of n=1.5 glass.
- **Analytic surfaces** — `source/geometry.c`: rays against spheres and planes
  in closed form, inside hits included (glass will need the far wall). The
  family grows milestone by milestone; there will never be triangles.
- **Camera as ray generator** — `source/camera.c`: pinhole camera returning
  the ray a pixel sees along, shared in shape by the CPU oracle and the
  GPU shader so their images stay comparable.
- **CPU reference tracer** — `source/cpu_trace.c`: hologram's oracle. Primary
  rays, Lambert under one sun with hard shadows, the M0 sky for misses; slow
  is fine, right is mandatory. GPU frames are diffed against it from M2 on.
- **Fixed-step accumulator** — `source/timestep.c`/`.h` ported verbatim from
  magnolia 0.3.0 together with its host test; the simulation side of the engine
  starts life already tested.
- **Display bring-up** — `source/display.c` owns the sokol window, device and
  swapchain, and drives a fullscreen-quad shader with per-frame uniforms
  (resolution, time): the surface the ray tracer renders through.
- **Game uniform blocks and readback** — a game hands display.c its own
  uniform struct (led by the built-in header) and gets it uploaded every
  frame; `holo_display_read_frame()` reads the presented frame back through a
  D3D11 staging texture, so the GPU's actual pixels can be held to the oracle.
- **The tracer on the GPU** — `shaders/trace.hlsl` is cpu_trace.c ported
  statement for statement; scene and camera arrive as one uniform block
  (`source/gpu_scene.c`, the same layout written in two languages).
- **Mirrors** — `source/geometry.c` gains finite rectangles (Gram-solved
  affine coordinates, so skewed panels stay honest); every material carries a
  mirror weight; both tracers walk reflections iteratively to 16 bounces,
  banking the matte share at each surface and tinting the throughput by the
  mirror's own albedo. Held by tests to the mirror-image property (looking
  through a mirror equals looking at the mirrored scene), to per-bounce
  attenuation (0.5^5 exactly after five bounces off half-red mirrors), and to
  the depth cap (trapped light returns black, not a hang).
- **The oracle as a callable** — `source/oracle.c` wraps the GPU-vs-CPU frame
  diff behind one function, so every example's --diff mode is three lines.
- **Glass** — `holo_fresnel()` in linalg.c is the real pair of Fresnel
  equations, s and p separately (M6's Stokes vectors will want them apart),
  held by tests to 4% at normal incidence, a vanishing p at Brewster's angle,
  reciprocity across the interface, and total reflection past the critical
  angle. Materials gain transmit and ior; spheres refract as volumes (rays
  bend in, bend out, and can be trapped), rects transmit as thin panes.
  The mirror walk becomes a small deterministic ray stack -- glass forks
  light -- with fixed caps, push order and cull threshold mirrored exactly
  in the shader, so the CPU and GPU drop the same branches. A trace test
  sums a glass ball's branch weights to the hand-computed 0.998464: glass
  neither makes nor eats light.
- **Spectral light** — `source/spectrum.c`: twelve fixed wavelengths across
  the visible band (fixed, not sampled -- the oracle diff needs the CPU and
  GPU tracing identical rays), Cauchy dispersion anchored at the sodium D
  line (BK7's Abbe number computes to its catalog 64), and the
  Wyman-Sloan-Shirley fits to the CIE 1931 color matching functions folding
  intensities to sRGB, normalized so a flat spectrum is exact white.
  `holo_trace_lambda()` walks one wavelength with a scalar throughput and
  glass refracting at n(lambda); the spectral and RGB pipelines agree to the
  float on neutral achromatic scenes, and part ways at the first dispersive
  surface -- which is the point.
- **Polarization** — `source/polar.c`: every spectral ray carries a
  detector-row Stokes accumulator and its transverse frame (all sources are
  unpolarized, so a path needs only the first row of its Mueller product --
  four floats, not sixteen). Interfaces rotate into their s/p or axis basis
  by double angles from dot products and apply Mueller matrices built from
  Fresnel amplitudes, TIR carrying its true phase retardance. Rects can be
  ideal polarizers or waveplates (retardance scaling 1/lambda, so a thick
  plate between crossed polarizers writes interference colors through the
  spectral loop). Held by tests to Malus's law at five angles, the
  three-polarizer paradox (0 crossed, exactly 1/8th with a 45-degree third),
  Brewster's fully polarized reflection, energy conservation per
  polarization, the quarter- and half-wave plates, and the 36.9-degree TIR
  phase a Fresnel rhomb is cut to.
- **sRGB display encoding** — the tracer works in linear radiance and now
  encodes to sRGB at its one display boundary (shader output, mirrored in
  the oracle's comparison), so dim physics -- an eighth of a wall through
  three polarizers -- reads as the eye would see it.
- **Walking** — `source/collision.c`: a capsule reduced to radius and
  height against a floor and axis-aligned walls, resolved one axis at a
  time so hitting a wall at an angle slides along it. Held by tests to what
  hands notice: landings stop at the floor, walls stop a radius away,
  slides keep full sideways speed, jumps come back down. All the physics
  Crystal Mirror Maze's player uses.
- **Input** — `source/input.c` folds sokol events to keys-held plus
  consumed mouse deltas, with capture built in (click to look, Escape to
  free, keys cleared on focus loss so nothing sticks).
- **Game hooks** — the display gains before_frame (simulate, then write
  the camera into the uniforms) and an event callback, alongside the
  existing after_frame; a game is now three callbacks and a scene.
- **Curved mirrors** — `holo_ray_dish()`: a cap of a conic of revolution
  in the language optical design quotes them (apex, axis, vertex radius of
  curvature, conic constant, rim), quadratic along the ray in the dish's
  own frame, sheet-clipped at the rim's sag. The tests hold it to the
  definitions: a paraboloid reflects every zone's parallel ray through
  R/2, an ellipsoid images focus onto focus, and both survive an arbitrary
  tilt of the frame. (Dishes are mirror or matte; curved glass -- lenses --
  waits for its own milestone. They do not yet throw shadows.)
- **The sun as a disk** — rays within a cone of sun_dir see a set
  intensity instead of the sky gradient, off by default. This is what
  makes focusing VISIBLE by backward tracing alone: at a paraboloid's
  focus every point of the dish reflects the eye into the sun, so the
  whole aperture flashes -- the solar-furnace test pins it at exactly the
  disk intensity on the focus and plain sky half a meter off it.
- **Diffraction gratings** — `holo_grating_order()` in linalg.c is the
  conical/off-plane vector grating equation: the groove component of the
  direction is conserved, the dispersion component picks up m lambda/d,
  and m = 0 falls out as exact specular. Rects can be reflection gratings
  (period, groove angle, fixed weights for orders m = -1, 0, +1, +2).
  Held by tests to Littrow retroreflection, the conical invariant, and an
  energy audit where the second order joins the sum exactly at the
  wavelength the equation admits it. Rendered live, a ruled panel paints
  the Rayleigh order-cutoff color bands and sweeps them as you walk.
- **The spectral walk is now CHAIN + FORK** — each ray walks as a chain,
  every surface continuing in place (reflections, filter transmission,
  the current grating order) and forking at most ONE side branch onto the
  stack (glass's transmitted ray, a grating's next-order revisit, carried
  in the depth field's high bits). One push per interaction is a hard
  ceiling: fxc, D3D11's shader compiler, silently corrupts the walk's
  stack arrays when any single branch pushes twice -- a defect that took
  a long forensic session to corner (per-grating cbuffer arrays also read
  back as garbage under dynamic indexing, hence the two scalar grating
  slots in the uniform block). The CPU mirrors the structure exactly, so
  the oracle diff stays meaningful; the m9 diff now agrees to a MAX error
  of 1/255 on a plain-floor scene.
- **Checked refraction at the critical edge** — both walks now check the
  refract discriminant before pushing the transmitted ray: at the razor
  edge of the critical angle, Fresnel's float arithmetic can say "not
  TIR" while refract's disagrees, and the unchecked call pushed an
  uninitialized direction (NaNs the oracle now also clamps defensively).
  The diff outlier bar moves 0.5% -> 0.75%: a dispersive ball traced at
  twelve wavelengths has twelve TIR rims of legitimate float-coin pixels.

- **The rect intersection, hoisted** — `holo_ray_rect` recomputed a cross, a
  normalize and a Gram solve on every ray against every panel, all of it
  ray-independent. `holo_rect_basis()` now computes the two solve vectors
  once per panel in `gpu_scene.c`, and the intersection is two dot products;
  the shaders derive the normal from the solve vectors rather than being sent
  it. That last part is measured, not assumed: shipping the normal as a third
  vector was SLOWER, because in sokol's GL path (no uniform blocks, a flat
  indexed array) one more indexed fetch costs more than the cross and
  normalize it saves -- 15.4ms against 11.4ms at 64 panels, 640x480,
  spectral. The block keeps its original 115 slots: the solve vectors replace
  the edges, which no tracer wanted in the first place.
- **A second tracer dialect** — `shaders/trace.glsl`: the HLSL tracer ported
  statement for statement into GLSL, serving GL 4.1 and GLES3/WebGL2 from one
  file (display.c prepends the version line and precision defaults). sokol's
  GL backend has no uniform buffer objects and takes at most sixteen named
  uniforms per block, so the scene arrives as a single `vec4 params[]` read by
  slot, with accessor macros restoring the field names; that slot map is held
  to `offsetof(HoloGpuScene, ...)` by `tests/test_gpu_layout.c`. Agrees with
  the oracle on all eight example scenes.
- **A third tracer dialect** — `shaders/trace.metal`, ported from the GLSL.
  MSL has no global uniforms, so the scene is threaded into the six
  functions that read it as a `constant float4 *params` argument; the
  bodies are unchanged, because the accessor macros only need something
  called `params` in scope. The varying carries `[[user(locn0)]]` on both
  sides since sokol compiles the two stages as separate Metal libraries,
  which match by attribute rather than by name. Type-checked as C++14
  against a `metal_stdlib` stand-in, but no Metal device has compiled or
  run it yet, and there is no macOS build to run it with.
- **Backend-agnostic display** — `source/display.c` now picks the shader
  dialect, vertex stage, compile targets and uniform-block description from
  the backend macro, loads the tracer through `holo_load_shader()` rather than
  eight copies of an `fopen`, and reads frames back on GL (`glReadPixels`) and
  Metal (blit to a shared staging texture, then `getBytes`) as well as D3D11,
  so the oracle survives leaving Windows. Only the D3D11 readback has ever
  run; the other two are written and untested.
- **The oracle, exported** — `holo_oracle_dump()` writes a comparison's inputs
  to disk (the uniform block as the shader receives it, and the CPU's encoded
  frame), so a tracer that cannot run in this process can still be held to the
  reference. Every example that takes `--diff` takes `--dump`.

### Examples

- **m0_window** — opens a 640×480 window and shades the quad with a
  ray-direction gradient sky: the first frame the future tracer will ever draw.
- **m1_cpu** — the oracle's first picture: three spheres on the checkered
  floor under an afternoon sun, written to `build\m1_cpu.ppm`.
- **m2_gpu** — the same scene traced live on the GPU; `m2_gpu --diff` reads
  the frame back and holds it to the CPU oracle (mean error under 1/255,
  outliers under 0.5% -- the run that landed this measured 0.012/255 mean
  with 0.001% outliers, all of them silhouette pixels).
- **m3_mirrors** — the shot this engine exists for: two facing mirror walls,
  a chrome sphere, a polished floor, all reflecting each other to real
  recursion depth. Screen-space reflection cannot draw this frame; the GPU
  version diffs against the oracle at 0.022/255 mean error.
- **m4_glass** — the classic proof of real refraction: the checker floor
  upside-down inside a ball lens, a tinted glass beside it, a standing pane
  that turns mirror toward grazing angles because Fresnel says so.
- **m5_spectral** — a crown ball and a flint ball in front of a white
  stripe: fringeless seen directly, gentle fringes through BK7, a real
  spectrum through the flint at five times the dispersion -- the same
  reason camera lenses pair the two glasses.
- **m6_polarization** — two windows onto a bright wall: a crossed pair
  with a 45-degree third polarizer glowing between them (the paradox that
  buries the absorption metaphor), and the same crossed pair around a
  full-wave plate glowing interference blue. Nothing painted; Mueller
  matrices times twelve wavelengths.
- **m7_room** — the vertical slice: a room built from the engine's whole
  vocabulary (facing mirrors, a glass window, a flint ball, a polarizer
  pane, a polished checker) that you walk through in first person -- WASD
  and mouse into a fixed-step walker with real collision, every frame
  traced spectrally with polarization. T toggles spectral; walk up to the
  flint ball and its TIR rings fringe into actual spectra.
- **m8_furnace** — a paraboloid dish aimed at a low sun with its focus
  parked at eye height on the path from spawn: walk forward and the sun's
  reflected image swells until, crossing the focus, the whole aperture
  flashes blinding white -- a solar furnace from the inside. A concave
  shaving mirror beside the path hangs the world upside down.
- **m9_spectrum** — two ruled panels (833 and 1430 lines/mm, grooves
  vertical) under a fat low sun: the coarse one shimmers, the fine one
  wears its order-cutoff spectrum as a color gradient that sweeps with
  your eye position, the way a CD tilts its colors. T toggles spectral
  and the colors vanish -- RGB light has no wavelength to sort.

### Repository

- House conventions from magnolia/adenosine: AGENTS.md (including the
  no-AI-attribution rule), VERSION as source of truth, Apache-2.0, host tests
  as standalone binaries under `tests/`.
- sokol vendored under `external/sokol/` (zlib licence).
- `build.sh` beside `build.bat`: Linux (OpenGL) and macOS (Metal), same
  no-library, compile-the-sources contract.
- `tools/gldiff` renders `shaders/trace.glsl` in a WebGL2 context and compares
  it to the CPU oracle using oracle.c's own arithmetic and bars, which is how
  the GL tracer is held to the reference from a host with no GL toolchain.
- `tools/bench` times the GPU against panel count, and A/Bs one tracer file
  against another on real hardware. It uses D3D11 timestamp queries rather
  than the frame clock, because with vsync on every number is the refresh
  interval and with it off the CPU runs ahead of the GPU; on backends with no
  GPU clock it falls back and says so. It has already overruled intuition
  once, on whether to ship the rect normal or derive it.
- `tools/metalcheck` type-checks `shaders/trace.metal` by compiling it as
  C++14 against a `metal_stdlib` stand-in, so the MSL tracer gets some
  verification on hosts that can neither compile nor run it. Names, arities
  and types only; attributes, linkage and rendering still need a Mac.
