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

### Repository

- House conventions from magnolia/adenosine: AGENTS.md (including the
  no-AI-attribution rule), VERSION as source of truth, Apache-2.0, host tests
  as standalone binaries under `tests/`.
- sokol vendored under `external/sokol/` (zlib licence).
