# AGENTS.md — hologram

Optics-first 3D game engine: a spectral ray tracer where light obeys physics.
C99 + sokol (vendored under `external/sokol/`), shaders in GLSL/HLSL. Version
0.1.0 (the `VERSION` file is the source of truth). Games compile hologram's
sources directly, magnolia-style — there is no library build. PolyForm
Noncommercial 1.0.0 (see NOTICE — deliberately unlike the Apache-2.0 2D
engines; hologram's commercial rights stay with magmacrunch media LLC).
Named after the Dag Henderson track "hologram of a dream" (magmacrunch music).

## AI Attribution

**No AI attribution.** Do not append `Co-Authored-By: Claude …`, "Generated with …",
or any similar trailer to commit messages, PR bodies, or release notes. If your
tooling adds such a line by default, remove it before committing.

## Thesis

The renderer is a real-time spectral ray tracer over analytic primitives, not a
rasterizer. Wavelength is sampled per path (dispersion falls out of n(λ));
polarization rides along as a Stokes vector (Mueller matrices at surfaces).
Mirrors reflect recursively because the rays actually bounce. Anything that
would fake an optical effect a tracer can do for real is out of style here.

## Layout

```
hologram.h              umbrella header games include
source/                 engine modules, one .c/.h pair each; pure arithmetic is
                        split from platform calls so it can be host-tested
                        (timestep.c is ported verbatim from magnolia and keeps
                        that discipline)
shaders/                shader source, one file per dialect (HLSL, GLSL, MSL);
                        each is a statement-for-statement twin of cpu_trace.c
external/sokol/         vendored sokol headers (zlib licence, see LICENSE there)
tests/                  host-side tests + harness.h; each test is its own binary
examples/               one runnable demo per milestone
tools/                  dev tools that are not the engine (gldiff: holds the
                        GLSL tracer to the oracle in a browser, for hosts with
                        no GL toolchain)
build.bat               Windows build (finds vcvars64, builds examples + tests)
```

## Conventions

- C99, `-Wall -Wextra`-clean (or `/W4` under MSVC). Comments explain why, not what.
- Fixed timestep for simulation; rendering reads interpolated state.
- Engine API prefix is `holo_`; internal seams keep their own prefixes.
- Every optical law lands with a host test asserting it against the closed-form
  answer before it lands in a shader. The CPU tracer is the oracle for the GPU.
- Commit as `magmacrunchmedia <magmacrunchmedia@gmail.com>`.
