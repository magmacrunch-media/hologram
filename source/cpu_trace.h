#ifndef HOLO_CPU_TRACE_H
#define HOLO_CPU_TRACE_H

/* The CPU reference tracer -- hologram's oracle.
 *
 * Every optical behaviour lands here first, in plain testable C, before it
 * lands in a shader; GPU frames are then diffed against images this file
 * renders. It is allowed to be slow and obliged to be right.
 *
 * M1 gave it primary rays, Lambert under one sun with hard shadows, and the
 * M0 sky. M3 adds mirrors: rectangles as surfaces, a mirror weight on every
 * material, and a bounce loop that follows reflections until the light is
 * spent. Wavelengths and Stokes vectors arrive in later milestones.
 */

#include "camera.h"

#define HOLO_MAX_SPHERES 8
#define HOLO_MAX_RECTS   8

/* Reflections deeper than this add nothing: with any real mirror tint the
   throughput is well under 1% by here, and the corridor's far end is a few
   pixels wide. The trace returns what it has gathered and stops. */
#define HOLO_MAX_BOUNCE 16

/* The fraction of the sun a shadowed point still shows. Not physics yet --
   a stand-in until bounced light exists to fill shadows honestly. */
#define HOLO_AMBIENT 0.1f

/* mirror = 0 is matte, 1 a pure mirror; between, the surface splits into a
   Lambert part and a reflected part. A mirror's reflection is tinted by its
   albedo -- silver is a color too. (Fresnel will make this angle-dependent
   in M4; a fixed weight is the honest simplification until then.) */
typedef struct {
    HoloV3 center;
    float  radius;
    HoloV3 albedo;
    float  mirror;
} HoloSphere;

typedef struct {
    HoloV3 corner;
    HoloV3 edge_u, edge_v;   /* lengths are the panel's size */
    HoloV3 albedo;
    float  mirror;
} HoloRect;

typedef struct {
    HoloSphere spheres[HOLO_MAX_SPHERES];
    int    sphere_count;
    HoloRect rects[HOLO_MAX_RECTS];
    int    rect_count;

    int    has_floor;
    float  floor_y;
    HoloV3 floor_a, floor_b;   /* 1m checker */
    float  floor_mirror;       /* a polished floor reflects a little */

    HoloV3 sun_dir;            /* unit, from the scene toward the sun */
    HoloV3 horizon, zenith;    /* the M0 sky */
} HoloScene;

/* The color a single ray sees, mirror bounces included. */
HoloV3 holo_trace_ray(const HoloScene *scene, HoloRay ray);

/* Render the whole frame into rgb (w*h*3 floats, rows top-down, linear
   0..1-ish -- tone mapping is the caller's problem, as it will be the
   swapchain's on the GPU). */
void holo_trace_image(const HoloScene *scene, const HoloCamera *cam,
                      int w, int h, float *rgb);

#endif
