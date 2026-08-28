#ifndef HOLOGRAM_H
#define HOLOGRAM_H

/* hologram -- an optics-first 3D game engine: a real-time spectral ray tracer
 * where light obeys physics.
 *
 * This is the umbrella header games include. It stays an index of the public
 * modules, magnolia-style: each module keeps its own header, and internal
 * seams (split out so their arithmetic can be host-tested) are deliberately
 * not listed here.
 */

#define HOLOGRAM_VERSION "0.1.0"

#include "source/display.h"   /* window, GPU device, fullscreen tracing surface */
#include "source/timestep.h"  /* fixed-step accumulator (ported from magnolia)  */

#endif
