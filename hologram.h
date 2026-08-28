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

#include "source/linalg.h"    /* vectors, reflection, Snell's law               */
#include "source/geometry.h"  /* rays against the analytic surfaces             */
#include "source/camera.h"    /* the camera as a ray generator                  */
#include "source/cpu_trace.h" /* the CPU reference tracer: hologram's oracle    */
#include "source/display.h"   /* window, GPU device, fullscreen tracing surface */
#include "source/gpu_scene.h" /* the scene as the shader's uniform block        */
#include "source/oracle.h"    /* holding the GPU frame to the CPU oracle        */
#include "source/timestep.h"  /* fixed-step accumulator (ported from magnolia)  */

#endif
