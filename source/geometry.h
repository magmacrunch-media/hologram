#ifndef HOLO_GEOMETRY_H
#define HOLO_GEOMETRY_H

/* Rays against analytic surfaces.
 *
 * hologram has no triangles: every surface in a scene is one of these
 * closed-form shapes, which is why the tracer can afford to be real-time and
 * why every intersection can be tested against algebra instead of against a
 * mesh. The family grows milestone by milestone: sphere, plane and finite
 * rectangle first, the conic dish in M8, and the Fresnel lens after v0.1 --
 * a whole stack of prism rings as one closed form, since a ring's tilt is a
 * function of its radius and never needs storing. Box and cylinder are
 * still to follow, if a game asks.
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

/* The facet tilt a Fresnel ring needs at radius r from the axis, for a lens
   of focal length f in glass of index n: the exact solution of
   arcsin(n sin a) - a = atan(r / f), the prism-deviation condition for
   collimated light inside the slab meeting a facet inclined by a and leaving
   toward the focus. Closed form -- sin(a + d) = n sin a rearranges to
   tan a = sin d / (n - cos d) -- so a ring's tilt is computed from its radius
   and never looked up, which is what lets a lens of forty rings be one
   primitive with no table and no dynamically indexed uniform.

   n is the DESIGN index, the D line, not n(lambda): a lens whose facets were
   cut for the wavelength being traced would be perfect at every colour,
   which is not a lens.

   Two limits fall out of the same algebra and the tests hold both. At r << f
   this is the thin prism, a = (r/f)/(n-1). And the usable belt ends at
   r = f sqrt(n^2 - 1), where a reaches the critical angle asin(1/n) and the
   exit ray grazes the facet; past it the formula keeps returning a finite
   angle on a spurious branch and the ring silently stops collimating. */
float holo_fresnel_tilt(float r, float focal, float n_design);

/* A Fresnel lens as ONE primitive: a slab of glass `thick` deep whose front
   face is cut into concentric rings `pitch` wide, from radius r0 out to
   `rim`. Ring k spans [r_k, r_k + pitch] with r_k = r0 + k pitch; its peak
   sits at r_k on the plane through `center` perpendicular to `axis`, its
   facet descends outward at the tilt holo_fresnel_tilt gives for r_k, and
   a vertical riser at the outer edge steps back up to the next peak -- the
   sawtooth a plano-convex lens collapses to. `axis` is unit and points out
   of the grooved face toward the focus, at center + focal * axis; the flat
   back face is at center - thick * axis, and the outer wall (and an inner
   one when r0 > 0) closes the solid, which matters because glass is traced
   as a volume and an open solid leaves the tracer's inside flag stranded.

   No ring is stored. Each is computed from its index, so forty rings cost
   no uniform slots beyond the eleven numbers above and no table that a
   shader would have to index. The ring a ray meets is found by seeding from
   the radius where the ray enters the slab and testing a fixed window of
   rings either side: the grooves are steep enough (up to 45 degrees at the
   critical-angle limit) that an oblique ray crossing one ring's span lands
   on a neighbour's facet, and a ray inside the glass crosses several before
   it surfaces. A fixed trip count with no early exit, deliberately, since
   that is the loop shape fxc has not miscompiled here.

   Two things the caller owes it: pitch well above HOLO_T_MIN, or a ray
   refracting into a facet meets the riser inside the guard and escapes
   through solid glass; and thick greater than pitch * tan(a_max), or the
   deepest groove cuts through the back face. Neither is checked here. */
#define HOLO_FRESNEL_WINDOW 3

int holo_ray_fresnel(HoloRay r, HoloV3 center, HoloV3 axis,
                     float focal, float n_design, float r0, float pitch,
                     float rim, float thick, HoloHit *hit);

/* An orthonormal basis around a unit axis, deterministic so the CPU and
   GPU build the same one. */
void holo_basis(HoloV3 axis, HoloV3 *u, HoloV3 *v);

#endif
