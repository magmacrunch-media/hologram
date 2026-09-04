# Changelog

All notable changes to the hologram engine are documented here.

## v0.2.0 (unreleased)

### The editor

`editor/` -- a page that runs `shaders/trace.glsl` in WebGL2 and flies a
camera around a scene while it renders. No build step and no dependency; it
reads the engine's own files in place, on the arrangement `tools/gldiff`
proved. Shows the primitive budget against the caps, and reloads the tracer
from disk on a keypress.

- `source/scene_json.c` writes a scene out as JSON. Write-only on purpose:
  no parser, so nothing at runtime gained a dependency or a failure mode.
  `holo_oracle_dump()` now emits `build/<name>_scene.json` beside the
  `params.bin` and `ref.bin` it already wrote, so every example that takes
  `--diff` opens in the editor with no change to its `main.c`.
- Floats are written at the shortest spelling that reads back bit-exact
  through `strtof`, so a scene round-trips exactly and still diffs like a
  file a person wrote.
- `editor/core/scene.js` is a fifth statement of the uniform block's layout,
  and is held to the four that came before it: it packs a scene C already
  packed and compares against `params.bin` float by float, in the page. All
  eight dumped examples pass, with every geometry field bit-identical.
- `tests/test_scene_json.c`, 58 checks.

The scene bench: the room as a list, an inspector, and the C it emits.

- `editor/core/schema.js` is what a primitive's fields ARE -- name, kind,
  range, meaning. The panels are generated from it and nothing in `ui/`
  knows what a sphere is, so a new material field on `HoloRect` is a line
  in a table rather than new UI. The help text is `cpu_trace.h`'s own
  comments, which is where the material model is actually explained.
- Fields the engine will ignore given the rest of the primitive -- `ior`
  with no transmit, `retard` on a polarizer, `grating_angle` with no
  period -- are greyed with the reason rather than hidden, and
  re-evaluate as you edit.
- Adding is refused at the cap, with the consequence named. Three gratings
  warns loudly: the third renders matte black on the GPU while the CPU
  oracle renders it correctly, which is a scene that passes its own
  `--diff` and ships wrong.
- `editor/core/emit.js` writes the scene back out as a `HoloScene`
  designated-initializer block plus its `holo_camera_make` call. Zero
  fields omitted, floats at the shortest spelling that reads back exact.
- `editor/roundtrip.c` proves it: a literal copied verbatim out of the
  panel, compiled and packed, reproduces the block `--dump` wrote. 840 of
  840 floats identical.
- Undo is magma-kit's `history.js`, copied into `editor/vendor/` with its
  provenance recorded rather than taking the kit's whole sync contract for
  one file. A slider drag is one undo entry.

The oracle panel: `tools/gldiff` absorbed.

- `editor/core/oracle.js` is `holo_oracle_diff`'s arithmetic, and the panel
  draws GPU, CPU and the difference at 8x, with oracle.c's verdict. It
  reproduces `gldiff.html` exactly on all eight dumped examples -- mean,
  max and outlier share -- with no second page and no second server.
- It always diffs the scene AS DUMPED, never the edited one: the CPU
  reference is a render of that scene, and a JavaScript `cpu_trace.c`
  would be a sixth statement of the tracer checked by nothing. The panel
  says so when the scene has been edited.
- Found in the process: **`oracle.c` and `gldiff.html` do not compute the
  same mean.** oracle.c breaks out of the channel loop on the first channel
  off by more than 8, so an outlier pixel's remaining channels never reach
  the sum, and `max` stops moving as well; gldiff sums all three. On
  m7_room that is 0.0888 against 0.1976 for identical pixels. The outlier
  share, computed the same way in both, agrees exactly.

  This reaches the README's pass table, whose D3D11 column is oracle.c's
  mean and whose WebGL2 column is gldiff's. Measured with one statistic the
  two columns very nearly coincide -- m4_glass 0.1161 both ways, m5_spectral
  0.1183, m8_furnace 0.0185 -- so WebGL2 agrees with the oracle about as
  closely as D3D11 does, and the table's apparent divergence is mostly the
  statistic changing between columns. gldiff.html's header no longer claims
  to use oracle.c's arithmetic. Which mean is the right one is an engine
  decision and nothing here has been changed to force it.

The sweep: a detector on the image, and one knob turned under it.

- A probe rectangle over the view reads its area as one number, and a
  sweep steps one scalar field of the selected primitive across a range,
  rendering each step and plotting what the probe saw.
- It measures the renderer instead of restating the theory beside it.
  Nothing in `editor/core/sweep.js` knows Malus's law; turning a polarizer
  with a detector behind it is what produces the cosine. A JavaScript copy
  of Fresnel, Malus, Cauchy and the grating equation would have been four
  more twins checked by nothing, answering questions about themselves.
- Values are decoded from sRGB to linear before averaging. `encode_u8`
  applies the transfer curve on the way out, so the bytes are not
  proportional to intensity and a cos^2 read straight off them is visibly
  wrong.
- Checked against the closed forms the host tests already pin, by a path
  sharing no code with them. Sweeping m6_polarization's crossed panel
  through 180 degrees reproduces cos^2 to a worst deviation of 0.0077,
  which is sRGB quantisation; the three-polarizer paradox measures 0.12350
  against the unpolarized wall, where test_polar.c pins an eighth, and a
  crossed pair with nothing between them reads a true zero.

Walking, and `source/walk_json.c`.

- A HoloWalkWorld is not part of a HoloScene -- the walls are
  hand-authored boxes that deliberately differ from the panels you can
  see, because what stops a player and what reflects light are not the
  same surface. So a dumped scene said nothing about where you can stand.
  The three examples that own a walker now dump their world too.
- The editor walks it with `G`: holo_walk_step through a twin, driven by
  the fixed-step accumulator at the 120 Hz the examples set, eyes 1.55
  above the feet. A flying camera will stand inside a wall and show a
  sightline that does not exist; this one cannot.
- That twin is the one nothing about the picture would catch drifting --
  a room you can walk through the wall of still renders perfectly -- so
  every dump carries a trace of 660 scripted steps recording what was
  commanded and what came out, and the editor replays it. The script is
  in the file rather than shared knowledge, so nothing has to be kept in
  step but the arithmetic. All three worlds replay bit-identical, with
  the bar at exactly zero rather than a tolerance.
- Mutation-testing that check found it was not enough. Every wall in a
  room starts at the floor, so the vertical span test never changes its
  answer and `height` could be altered undetected; and a scripted walk
  only meets the walls it happens to pass, so a wall could be deleted
  undetected too. build/walk_selftest.json is a world built to be awkward
  about exactly that -- a plain wall, a curb low enough to jump, and an
  overhang clearing the floor by less than a walker's height -- and it
  catches both.
- holo_json_float is exported from scene_json.h so both writers spell a
  float the same way. walk_json.h joins the umbrella header, since unlike
  scene_json it is called from a game's own main.

Saving, without a desktop wrapper after all.

- Ctrl+S saves, Ctrl+Shift+S saves elsewhere, Ctrl+O opens. The first
  save asks where and the rest are silent, because the File System Access
  API hands back a file handle -- which is most of what the desktop
  wrapper was wanted for here. Where it is missing a save falls back to a
  download, and says so rather than pretending.
- Every edit also writes a draft to localStorage, restored on load, so a
  closed tab does not lose work that never reached a file.
- A save never overwrites build/<name>_scene.json. That file is what
  --dump produced and the params.bin and ref.bin beside it are renders OF
  it; an edited scene written over it would leave the packer banner and
  the oracle panel reporting confident verdicts about a different room.
  The packer check now runs against the scene as fetched rather than the
  one being edited, a saved file records that the editor wrote it, and a
  document carrying that mark reports UNCHECKED and no reference instead
  of inventing an answer.
- The saved format is scene_json.c's, to the byte: serialising each of
  the eight dumped scenes and stripping the provenance block reproduces
  the C-written file exactly. Two spellings had to be matched to get
  there -- printf pads an exponent to two digits where JavaScript does
  not, and prints negative zero with its sign where String(-0) is "0".

The plan view, and authoring the walk world.

- The tracer cannot draw a collision box -- they are not optical, so in
  the first-person view a wall is invisible and the only evidence of one
  is being stopped. That is how crystal-mirror-maze's walls are authored
  today: written as C, compiled, walked into.
- editor/ui/planview.js draws the room from above, where an axis-aligned
  box loses only its height and the labels carry that. Drag to move, drag
  a corner to resize, wheel to zoom; add and delete against HOLO_MAX_WALLS.
- The scene's panels and spheres are drawn faintly underneath, which is
  the point of the view and not decoration: a collision world deliberately
  does not match the geometry you can see -- m7_room's mirrors are at
  x = +-4 while the walls that stop you are 0.3-thick slabs behind them --
  so the mistakes worth catching are a wall drifted from the mirror it
  backs, a doorway quietly closed, a pane you can walk through. Nothing is
  derived from a panel; that would make them one surface.
- The walker's radius is drawn around your position, so a gap can be
  judged before you try to walk through it.
- emit.js gains worldToC, and it is checked the way the scene emitter is:
  editor/worldtrip.c compiles a literal copied out of the panel, hands it
  to holo_walk_write_json, and the resulting world object is identical to
  the dump's. `save walls` writes hologram/walk/1 without a trace, since a
  trace is a record of the C stepping a world and the editor cannot
  honestly produce one.
- Editing walls withdraws the trace's verdict rather than breaking it. A
  dumped trace describes the world it was recorded from, so the world is
  kept twice -- the one you edit and walk in, and the pristine one the
  check replays -- and the line reads "world edited, trace withdrawn"
  once they differ. The twin stays checked throughout, because the
  synthetic selftest world is one no edit can reach.
- Walls ride the scene's undo stack, so Ctrl+Z means the last thing you
  did whichever panel you did it in, and a wall drag collapses to one
  entry the way a slider drag does.

Clicking things, and source/pick_json.c.

- Left-click picks whatever is under the cursor and drags it;
  right-drag looks. Pointer lock is gone with them: locking the cursor is
  right for a game and wrong for an editor, where the cursor is what you
  point at things with.
- A drag moves the primitive's origin on a plane through it, horizontal
  by default and vertical with shift. Which plane needed care: a ray
  misses the ground plane when the view is level (parallel to it) AND
  when the view is angled slightly up and the object sits below eye
  level (the plane is behind the ray). Both fall back to a plane facing
  the camera, which always has an intersection.
- Answering what is under a pixel means intersecting the scene, since the
  tracer returns a colour and not an identity. So editor/core/pick.js is a
  copy of geometry.c's intersections and cpu_trace.c's nearest_hit, and
  like every other copy here it is held to the engine: every --dump now
  writes build/<name>_pick.json, a 64x48 grid of rays through the camera
  and the primitive the ENGINE found nearest along each. All eight
  examples agree exactly, 3072 of 3072.
- The traversal order is copied deliberately. nearest_hit walks spheres,
  rects, dishes, floor, keeping a hit only on a strict t < best, so an
  exact tie goes to whichever was tested first -- which is what decides
  the answer wherever two surfaces touch.
- The check paid for itself at once. The first rayDish was written from
  geometry.h's description rather than its source and missed both the z
  clipping that keeps an ellipsoid's far half and a hyperboloid's second
  sheet out of the dish, and the fall-through to the far root that
  happens every time you look into a concave mirror. It put a dish in
  front of the floor across 6.6% of m8_furnace, and nothing about the
  rendered image would have shown it.
- editor/serve.py: the same static server with Cache-Control: no-store.
  Everything about this page is edit-a-file-and-reload, and a browser
  holding a stale script that looks current is an afternoon lost to
  reading code that is already correct.

A cost panel, and tools/bench --json.

- bench gains --json, writing build/bench.json: the same table it prints,
  plus the backend and which clock produced it. A reader that mistakes a
  frame-clock number for a GPU-timestamp one is doing the thing bench's
  header warns about at length, so the file says which it is.
- The editor times the tracer on its own surface, with GPU timestamps
  through EXT_disjoint_timer_query_webgl2 where the browser has it and a
  differently-named fallback where it does not. Frames are issued back to
  back and collected afterwards: polling each result before the next draw
  stalls between frames and measures a different workload.
- It is not bench and does not claim to be. bench asks what a panel costs
  on the backend a game ships; this asks what THIS room costs, which bench
  cannot, and answers it in WebGL2, which is not that backend.
- The obvious comparison turned out to be wrong, and measuring it is what
  showed that. Putting bench's "vs 1 panel" ratio beside the editor's
  looks defensible -- a ratio should survive a change of backend where
  milliseconds do not -- but bench builds a synthetic scene of N panels
  and nothing else, while the editor truncates a real room's rects and
  keeps everything else. In m7_room the fixed cost swamps the panels, so
  the editor's curve is flat where bench's climbs 9.44x. Different
  questions; neither the milliseconds nor the ratios cross between them,
  and bench's figures now sit under their own heading saying so.
- So the ladder starts at zero panels -- the cost of everything that is
  not a panel -- and every row above reports what the panels add, which is
  the number an edit moves.
- And it says when it cannot tell. A difference below the measurement's
  own noise comes out negative about half the time, and a table reporting
  that a panel made the frame cheaper has stopped describing the
  renderer; rows inside the zero stage's interquartile spread read "below
  noise" instead. On m7_room in WebKit's WebGL every row does, and the
  panel says so and points at the spectral row: 8.61 ms spectral against
  0.58 ms in RGB, nearly fifteen to one for the twelve wavelengths.

The cost panel could not tell anything from anything, and said so wrongly.

- Every row read "below noise" on every backend, including a real GPU. The
  threshold was comparing each difference against the interquartile spread
  of INDIVIDUAL FRAMES, which is how much one frame differs from the next
  and not the uncertainty of a median. Frame jitter of 0.5 ms across sixty
  frames leaves the median good to about 0.08, so differences it could
  comfortably resolve were being declared unmeasurable.
- It now uses the standard error of the median -- sigma from the IQR, then
  the median's own constant -- and asks a difference to clear two of them,
  one from each stage.
- Because that error falls as the square root of the frame count, a row
  that still cannot be resolved now reports how many frames WOULD resolve
  it. "below noise" becomes "needs ~151 frames", which is something to do
  rather than a dead end.
- A resolved NEGATIVE difference is reported as drift rather than as a
  measurement. Stages run in sequence, so anything changing over a run --
  clocks ramping, the machine warming -- lands on the later ones and looks
  like a panel-count effect; a panel cannot make a frame cheaper.
- Verified against directly measured data: at 32 frames m7_room's rows are
  unresolved and ask for 67 to 191 more; at 400 the two positive ones
  resolve and the negative one is flagged.

The editor can open a game's rooms, not just the examples.

- editor/serve.py --mount NAME=PATH serves another repository's build
  directory read-only, and ?dir= points the editor at it. The rooms worth
  editing live in games, which are separate repositories; copying their
  dumps into the engine's build/ works once and then goes stale on the
  next rebuild with nothing to say so, which is exactly the failure the
  rest of this is arranged to prevent.
- Only the dumps move. The tracer stays this repository's shaders/, since
  a game's copy is one its build script made and what the editor should
  show is the one the oracle holds to the CPU reference.
- The header names the directory a scene came from whenever it is not the
  engine's own, so the room on screen cannot be quietly the wrong one.
- Mounted paths are resolved and confined to their mount; the engine holds
  no list of the games that consume it.

The game release: whatever Crystal Mirror Maze development asks of the
engine lands here.

## v0.1.0

The founding release: everything from the M0 window through the M9 gratings,
and the portability work that followed — one tracer in three dialects (HLSL,
GLSL, MSL) held to the CPU oracle, builds for Windows and Linux with macOS
written and unproven, the gldiff/metalcheck/bench tools, and the scene sized
to 24 panels for the game this engine exists for. Crystal Mirror Maze
development starts against this release.

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
- **`HOLO_MAX_RECTS` raised from 8 to 24** — the panel allowance Crystal
  Mirror Maze needs, sized to the measured frame budget rather than
  aspiration (24 panels cost 2.17ms on a desktop GPU at 640x480 spectral,
  which scales to roughly a Deck frame). The uniform block grows to 211
  float4 slots, still inside WebGL2's guaranteed 224-vector floor, so the
  web path survives. The hand-carried slot maps in the GLSL and MSL tracers
  moved sixteen numbers each; `test_gpu_layout` held every one to
  `offsetof`, and all eight oracle diffs are unchanged on D3D11 and on
  Linux GL.
- **The fullscreen triangle rides a real vertex buffer** instead of being
  derived from SV_VertexID with nothing bound. Added on the suspicion that
  the bufferless draw was what garbled the frame under Wine; the suspicion
  was refuted -- the frame was byte-identical either way -- but the buffer
  stays, because 24 bytes buys the draw-call shape every backend treats as
  the common case. All eight oracle diffs held to the same numbers across
  the change, on D3D11 and on GL.
- **The Windows binary runs correctly under Wine**, which is the Proton
  question in all but name, with one caveat that took an evening to find:
  hologram compiles HLSL at runtime, Wine's builtin d3dcompiler miscompiles
  the tracer silently (every pixel wrong, no diagnostic), and Microsoft's
  d3dcompiler_47.dll beside the exe fixes it completely -- all eight oracle
  diffs green through Wine, WineD3D and software GL. A Wine or Proton build
  must ship that DLL or precompile its shaders.
- **The Linux build runs.** `build.sh` was written but never executed; the
  first attempt found two compile stoppers -- a missing `stdlib.h` that MSVC
  had been forgiving about, and `clock_gettime` hidden from sokol by
  `-std=c11` being strict ISO rather than gnu (the POSIX feature macro now
  goes on sokol's translation unit alone, so hologram's own sources stay
  strict). With those fixed, all eight examples build and every one agrees
  with the oracle through the GL readback, which had also never run. Under
  Mesa's software rasteriser the agreement is four decimal places, because
  it does the arithmetic the way cpu_trace.c does.
- `holo_load_shader_from()` loads a tracer from a caller-chosen path, still
  prepending the dialect preamble. `tools/bench` had been reading the file
  itself, which is invisible on D3D11 (empty preamble) and fatal on GL, where
  the tracer then compiles as GLSL 1.10.
- `build.sh` beside `build.bat`: Linux (OpenGL) and macOS (Metal), same
  no-library, compile-the-sources contract.
- `tools/gldiff` renders `shaders/trace.glsl` in a WebGL2 context and compares
  it to the CPU oracle using oracle.c's own arithmetic and bars, which is how
  the GL tracer is held to the reference from a host with no GL toolchain.
- `tools/bench` times the GPU against panel count, and A/Bs one tracer file
  against another on real hardware. It uses D3D11 timestamp queries and GL
  `GL_TIME_ELAPSED` queries rather
  than the frame clock, because with vsync on every number is the refresh
  interval and with it off the CPU runs ahead of the GPU; on backends with no
  GPU clock it falls back and says so. It has already overruled intuition
  once, on whether to ship the rect normal or derive it.
- `tools/metalcheck` type-checks both halves of the Metal side on hosts that
  can neither compile nor run them: `shaders/trace.metal` as C++14 against a
  `metal_stdlib` stand-in, and the frame readback in `display.c` as
  Objective-C with ARC against a Metal stand-in, which needs clang because
  gcc's Objective-C has no ARC. Names, arities, types, selectors and bridge
  casts; whether those selectors are Apple's, whether the attributes are
  right, whether the stages link and whether any of it renders still need a
  Mac.
