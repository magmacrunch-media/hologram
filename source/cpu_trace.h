#ifndef HOLO_CPU_TRACE_H
#define HOLO_CPU_TRACE_H

/* The CPU reference tracer -- hologram's oracle.
 *
 * Every optical behaviour lands here first, in plain testable C, before it
 * lands in a shader; GPU frames are then diffed against images this file
 * renders. It is allowed to be slow and obliged to be right.
 *
 * M1 scope: primary rays against spheres and a checkered floor, one sun,
 * Lambert shading with hard shadows, the M0 sky for misses. Bounces,
 * wavelengths and Stokes vectors arrive in later milestones.
 */

#include "camera.h"

#define HOLO_MAX_SPHERES 8

/* The fraction of the sun a shadowed point still shows. Not physics yet --
   a stand-in until bounced light exists to fill shadows honestly. */
#define HOLO_AMBIENT 0.1f

typedef struct {
    HoloV3 center;
    float  radius;
    HoloV3 albedo;
} HoloSphere;

typedef struct {
    HoloSphere spheres[HOLO_MAX_SPHERES];
    int    sphere_count;

    int    has_floor;
    float  floor_y;
    HoloV3 floor_a, floor_b;   /* 1m checker */

    HoloV3 sun_dir;            /* unit, from the scene toward the sun */
    HoloV3 horizon, zenith;    /* the M0 sky */
} HoloScene;

/* The color a single ray sees. */
HoloV3 holo_trace_ray(const HoloScene *scene, HoloRay ray);

/* Render the whole frame into rgb (w*h*3 floats, rows top-down, linear
   0..1-ish -- tone mapping is the caller's problem, as it will be the
   swapchain's on the GPU). */
void holo_trace_image(const HoloScene *scene, const HoloCamera *cam,
                      int w, int h, float *rgb);

#endif
