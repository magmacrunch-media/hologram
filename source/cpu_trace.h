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
#define HOLO_MAX_DISHES  4

/* Reflections deeper than this add nothing: with any real mirror tint the
   throughput is well under 1% by here, and the corridor's far end is a few
   pixels wide. The trace returns what it has gathered and stops. */
#define HOLO_MAX_BOUNCE 16

/* Glass splits light, so the walk keeps a small stack of pending rays
   instead of a single continuation. The caps bound the work per pixel and
   make branch-dropping deterministic -- the CPU and GPU drop the exact same
   branches, which the oracle diff depends on. A branch whose brightest
   channel falls under HOLO_MIN_TP could move a pixel by less than half a
   level, so it is not worth following. */
#define HOLO_MAX_RAYS 32
#define HOLO_STACK    16
#define HOLO_MIN_TP   0.002f

/* The fraction of the sun a shadowed point still shows. Not physics yet --
   a stand-in until bounced light exists to fill shadows honestly. */
#define HOLO_AMBIENT 0.1f

/* A material is three shares that sum to at most 1: mirror (metallic
   reflection, tinted by albedo -- silver is a color too), transmit (glass),
   and the matte remainder (Lambert). Glass brings its own reflection: the
   Fresnel equations split the transmit share between refraction and an
   untinted dielectric reflection, angle by angle, so a glass surface goes
   mirror-like at grazing incidence because physics says so, not because a
   parameter does. A sphere's glass is a volume (rays bend in and out and can
   be trapped by TIR); a rect's is a thin pane (direction unchanged, one
   Fresnel interface -- a window, not a prism). */
typedef struct {
    HoloV3 center;
    float  radius;
    HoloV3 albedo;
    float  mirror;
    float  transmit;
    float  ior;              /* at the sodium D line; meaningful when transmit > 0 */
    float  disperse;         /* Cauchy B in um^2; 0 = achromatic glass */
} HoloSphere;

/* A rect can also be an ideal optical filter instead of glass:
   HOLO_POLARIZER passes the component along its axis (Malus does the
   rest); HOLO_WAVEPLATE retards p against s about its axis by `retard`
   radians at the sodium D line, scaling as 1/lambda the way a zero-order
   plate does -- which is why a thick plate between crossed polarizers
   shows interference colors in the spectral path. Filters ignore mirror /
   transmit / ior; polarization physics exists in the spectral pipeline,
   and the RGB path approximates a polarizer as a flat 50% absorber. */
#define HOLO_FILTER_NONE      0
#define HOLO_POLARIZER        1
#define HOLO_WAVEPLATE        2

typedef struct {
    HoloV3 corner;
    HoloV3 edge_u, edge_v;   /* lengths are the panel's size */
    HoloV3 albedo;
    float  mirror;
    float  transmit;
    float  ior;
    float  disperse;
    int    filter;           /* HOLO_FILTER_NONE / POLARIZER / WAVEPLATE */
    float  filter_angle;     /* axis, radians from edge_u toward edge_v */
    float  retard;           /* waveplate retardance at the D line, radians */
} HoloRect;

/* A curved mirror, in the language optics quotes them: apex, axis, vertex
   radius of curvature, conic constant, rim. Mirror or matte only -- curved
   glass (lenses) waits for a milestone of its own. */
typedef struct {
    HoloV3 apex;
    HoloV3 axis;             /* unit, out of the bowl */
    float  curv_r;
    float  conic_k;
    float  rim;
    HoloV3 albedo;
    float  mirror;
} HoloDish;

typedef struct {
    HoloSphere spheres[HOLO_MAX_SPHERES];
    int    sphere_count;
    HoloRect rects[HOLO_MAX_RECTS];
    int    rect_count;
    HoloDish dishes[HOLO_MAX_DISHES];
    int    dish_count;

    int    has_floor;
    float  floor_y;
    HoloV3 floor_a, floor_b;   /* 1m checker */
    float  floor_mirror;       /* a polished floor reflects a little */

    HoloV3 sun_dir;            /* unit, from the scene toward the sun */
    HoloV3 horizon, zenith;    /* the M0 sky */

    /* The sun as a visible disk: rays within acos(sun_disk_cos) of sun_dir
       see sun_disk_intensity instead of the gradient. Zero intensity turns
       it off (the default). This is what makes focusing visible: a mirror
       that sends your eye-ray into the sun shows you the sun, and at a
       paraboloid's focus every point of the dish does. */
    float  sun_disk_cos;
    float  sun_disk_intensity;
} HoloScene;

/* The color a single ray sees, mirror bounces included. RGB light: glass
   refracts at its D-line index, dispersion invisible. */
HoloV3 holo_trace_ray(const HoloScene *scene, HoloRay ray);

/* The intensity a single ray sees at one wavelength: albedos read through
   holo_albedo_at, glass refracting at n(lambda). The walk, its caps and its
   culls are the same as holo_trace_ray's -- one wavelength at a time is the
   only difference. */
float holo_trace_lambda(const HoloScene *scene, HoloRay ray, float lambda_um);

/* The color a single ray sees spectrally: HOLO_WAVELENGTHS traces of
   holo_trace_lambda, folded through the CIE weights. Where no dispersive
   glass is struck this agrees with holo_trace_ray on neutral scenes; where
   it is, wavelengths part ways and fringes are real. */
HoloV3 holo_trace_ray_spectral(const HoloScene *scene, HoloRay ray);

/* Render the whole frame into rgb (w*h*3 floats, rows top-down, linear
   0..1-ish -- tone mapping is the caller's problem, as it will be the
   swapchain's on the GPU). spectral != 0 renders through
   holo_trace_ray_spectral. */
void holo_trace_image(const HoloScene *scene, const HoloCamera *cam,
                      int w, int h, float *rgb);
void holo_trace_image_spectral(const HoloScene *scene, const HoloCamera *cam,
                               int w, int h, float *rgb);

#endif
