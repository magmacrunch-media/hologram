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
  statement for statement; scene and camera arrive as one uniform block.

### Examples

- **m0_window** — opens a 640×480 window and shades the quad with a
  ray-direction gradient sky: the first frame the future tracer will ever draw.
- **m1_cpu** — the oracle's first picture: three spheres on the checkered
  floor under an afternoon sun, written to `build\m1_cpu.ppm`.
- **m2_gpu** — the same scene traced live on the GPU; `m2_gpu --diff` reads
  the frame back and holds it to the CPU oracle (mean error under 1/255,
  outliers under 0.5% -- the run that landed this measured 0.012/255 mean
  with 0.001% outliers, all of them silhouette pixels).

### Repository

- House conventions from magnolia/adenosine: AGENTS.md (including the
  no-AI-attribution rule), VERSION as source of truth, Apache-2.0, host tests
  as standalone binaries under `tests/`.
- sokol vendored under `external/sokol/` (zlib licence).
