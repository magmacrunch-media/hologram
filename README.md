# hologram

An optics-first 3D game engine by [magmacrunch media](https://magmacrunch.com):
a real-time spectral ray tracer where light obeys physics. Mirrors reflect
recursively, prisms cast real spectra, polarizers extinguish at the angles
Malus says they should — because the renderer traces wavelengths, not RGB
triples, and carries polarization on every ray.

Named after the Dag Henderson track "hologram of a dream", published by
magmacrunch music.

Current version: **0.1.0** — see [CHANGELOG.md](CHANGELOG.md).

## Why a ray tracer

The games this engine exists for (starting with Crystal Mirror Maze) are built
entirely from analytic primitives — planes, boxes, spheres, cylinders, prisms,
conics — and rendered at low resolution by design. Closed-form intersections at
640×480 are exactly the workload a fullscreen-shader ray tracer can afford in
real time, and a tracer is the only renderer in which curved mirrors,
refraction, dispersion, and polarization are *correct* instead of faked.

## Design

- **C99 engine, GLSL/HLSL shaders**, on [sokol](https://github.com/floooh/sokol)
  (vendored, zlib licence) for the window, GPU device, and swapchain across
  D3D11 / Metal / GL / WebGPU.
- **The tracing runs on the GPU** as a fullscreen-quad fragment shader over a
  small scene of analytic primitives.
- **A CPU reference tracer is the oracle**: every optical law is host-tested
  against its closed-form answer, and GPU frames are diffed against CPU renders.
- **Fixed-timestep simulation** (`source/timestep.c`, ported verbatim from
  [magnolia](https://github.com/magmacrunchmedia/magnolia)).

Games compile the engine's sources directly, magnolia-style; there is no
library build and no package registry.

## Building (Windows)

Requires Visual Studio (MSVC). No other dependencies.

```
build.bat          # builds the current example into build\
build.bat test     # builds and runs the host tests
```

## Roadmap

M0 window/quad/uniforms → M1 math + CPU tracer → M2 GPU parity → M3 mirrors →
M4 glass → M5 spectral → M6 polarization → M7 game layer → M8 curved mirrors →
M9 diffraction gratings.

## License

[Apache-2.0](LICENSE). Vendored sokol headers keep their zlib licence
(`external/sokol/LICENSE`).
