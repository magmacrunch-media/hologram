#ifndef HOLO_GEOMETRY_H
#define HOLO_GEOMETRY_H

/* Rays against analytic surfaces.
 *
 * hologram has no triangles: every surface in a scene is one of these
 * closed-form shapes, which is why the tracer can afford to be real-time and
 * why every intersection can be tested against algebra instead of against a
 * mesh. The family grows milestone by milestone (sphere and plane now; box,
 * cylinder, prism, conic to follow).
 */

#include "linalg.h"

/* No hit below this t: a ray leaving a surface must not immediately find the
   surface it left through float error. */
#define HOLO_T_MIN 1e-3f

typedef struct {
    HoloV3 origin;
    HoloV3 dir;      /* unit */
} HoloRay;

typedef struct {
    float  t;        /* distance along the ray, > HOLO_T_MIN */
    HoloV3 point;
    HoloV3 normal;   /* unit, out of the surface on the arriving side */
} HoloHit;

/* Each returns 1 and fills *hit on the nearest intersection past HOLO_T_MIN,
   or returns 0 leaving *hit alone. */
int holo_ray_sphere(HoloRay r, HoloV3 center, float radius, HoloHit *hit);
int holo_ray_plane(HoloRay r, HoloV3 point, HoloV3 normal, HoloHit *hit);

/* A finite parallelogram: corner plus two edge vectors (not unit -- their
   lengths are the panel's size). This is what a mirror is made of. */
int holo_ray_rect(HoloRay r, HoloV3 corner, HoloV3 edge_u, HoloV3 edge_v,
                  HoloHit *hit);

/* Everything about a rectangle that does not depend on the ray: the unit
   normal, and the two vectors that turn a point on the plane into affine u
   and v with one dot product each.

   holo_ray_rect recomputes these on every call, which means every ray
   against every panel: a cross, a normalize and five dot products of work
   that only depends on the panel. A renderer with more than a couple of
   mirrors wants them hoisted -- gpu_scene.c computes them once and ships
   them in the uniform block, and the tracers use holo_ray_rect_pre.

   solve_u and solve_v are the Gram solve folded flat: where the long form
   computes ru, rv and det and divides, u is just dot(rel, solve_u). */
void holo_rect_basis(HoloV3 edge_u, HoloV3 edge_v,
                     HoloV3 *normal, HoloV3 *solve_u, HoloV3 *solve_v);

/* holo_ray_rect against a panel whose basis is already in hand. */
int holo_ray_rect_pre(HoloRay r, HoloV3 corner, HoloV3 normal,
                      HoloV3 solve_u, HoloV3 solve_v, HoloHit *hit);

/* A dish: a cap of a conic of revolution, in the language optical design
   quotes them -- apex point, axis (unit, pointing out of the bowl), vertex
   radius of curvature R, conic constant K (0 a sphere, -1 a paraboloid,
   -e^2 an ellipsoid, below -1 a hyperboloid), clipped at rim radius. A
   paraboloid focuses parallel light at R/2 above the apex because this
   intersection and its normal say so; the tests hold both to that. */
int holo_ray_dish(HoloRay r, HoloV3 apex, HoloV3 axis,
                  float curv_r, float conic_k, float rim, HoloHit *hit);

/* An orthonormal basis around a unit axis, deterministic so the CPU and
   GPU build the same one. */
void holo_basis(HoloV3 axis, HoloV3 *u, HoloV3 *v);

#endif
