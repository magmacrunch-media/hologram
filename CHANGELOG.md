# Changelog

All notable changes to the hologram engine are documented here.

## v0.1.0 (unreleased)

The founding release: repository skeleton, the M0 window, and the seams every
later milestone builds on.

### Engine core

- **Fixed-step accumulator** — `source/timestep.c`/`.h` ported verbatim from
  magnolia 0.3.0 together with its host test; the simulation side of the engine
  starts life already tested.
- **Display bring-up** — `source/display.c` owns the sokol window, device and
  swapchain, and drives a fullscreen-quad shader with per-frame uniforms
  (resolution, time): the surface the ray tracer will render through from M2 on.

### Examples

- **m0_window** — opens a 640×480 window and shades the quad with a
  ray-direction gradient sky: the first frame the future tracer will ever draw.

### Repository

- House conventions from magnolia/adenosine: AGENTS.md (including the
  no-AI-attribution rule), VERSION as source of truth, Apache-2.0, host tests
  as standalone binaries under `tests/`.
- sokol vendored under `external/sokol/` (zlib licence).
