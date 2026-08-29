/* Rays against the analytic surfaces, held to cases solvable in your head:
 * a ray 5m from a unit sphere hits it at t = 4, an inside ray finds the far
 * wall with the normal flipped to meet it, a parallel ray never finds a
 * plane at all.
 *
 *   build.bat test
 */
#include <math.h>
#include <stdio.h>
#include "harness.h"
#include "geometry.h"

static void test_sphere(void) {
    printf("geometry: ray vs sphere\n");
    HoloHit h;

    /* Down the z axis at the unit sphere: enters at z = 1, so t = 4. */
    HoloRay r = { .origin = hv3(0, 0, 5), .dir = hv3(0, 0, -1) };
    check_int(holo_ray_sphere(r, hv3(0, 0, 0), 1, &h), 1, "head-on hits");
    check_close(h.t, 4.0f, "at t = 4");
    check_close(h.point.z, 1.0f, "on the near surface");
    check_close(h.normal.z, 1.0f, "normal back at the ray");

    /* 2m off axis misses a unit sphere entirely. */
    r.origin = hv3(0, 2, 5);
    check_int(holo_ray_sphere(r, hv3(0, 0, 0), 1, &h), 0, "offset ray misses");

    /* Grazing exactly at the top: tangent counts as a hit at y = 1. */
    r.origin = hv3(0, 1, 5);
    check_int(holo_ray_sphere(r, hv3(0, 0, 0), 1, &h), 1, "tangent ray touches");
    check_close(h.point.y, 1.0f, "at the pole");

    /* From the center: the near root is behind the T_MIN guard in spirit
       (negative), so the far wall answers, normal flipped inward. */
    r.origin = hv3(0, 0, 0);
    check_int(holo_ray_sphere(r, hv3(0, 0, 0), 1, &h), 1, "inside ray exits");
    check_close(h.t, 1.0f, "through the far wall");
    check_close(h.normal.z, 1.0f, "normal flipped to face the ray");

    /* The sphere behind the origin is not in front of the ray. */
    r.origin = hv3(0, 0, -5);
    check_int(holo_ray_sphere(r, hv3(0, 0, 0), 1, &h), 0, "behind means miss");
}

static void test_plane(void) {
    printf("geometry: ray vs plane\n");
    HoloHit h;

    HoloRay r = { .origin = hv3(3, 2, -1), .dir = hv3(0, -1, 0) };
    check_int(holo_ray_plane(r, hv3(0, 0, 0), hv3(0, 1, 0), &h), 1, "fall to the floor");
    check_close(h.t, 2.0f, "2m down");
    check_close(h.point.x, 3.0f, "landing x");
    check_close(h.point.z, -1.0f, "landing z");
    check_close(h.normal.y, 1.0f, "floor faces up");

    /* Walking parallel to the floor never lands. */
    r.dir = hv3(1, 0, 0);
    check_int(holo_ray_plane(r, hv3(0, 0, 0), hv3(0, 1, 0), &h), 0, "parallel misses");

    /* From under the floor the same plane faces down at the ray. */
    r.origin = hv3(0, -2, 0);
    r.dir = hv3(0, 1, 0);
    check_int(holo_ray_plane(r, hv3(0, 0, 0), hv3(0, 1, 0), &h), 1, "hit from below");
    check_close(h.normal.y, -1.0f, "normal on the arriving side");

    /* The floor behind the ray does not count. */
    r.origin = hv3(0, 2, 0);
    check_int(holo_ray_plane(r, hv3(0, 0, 0), hv3(0, 1, 0), &h), 0, "receding ray misses");
}

static void test_rect(void) {
    printf("geometry: ray vs rectangle\n");
    HoloHit h;

    /* A 2x2 panel in the xy plane. */
    HoloV3 corner = hv3(0, 0, 0), eu = hv3(2, 0, 0), ev = hv3(0, 2, 0);
    HoloRay r = { .origin = hv3(1, 1, 5), .dir = hv3(0, 0, -1) };
    check_int(holo_ray_rect(r, corner, eu, ev, &h), 1, "center of the panel");
    check_close(h.t, 5.0f, "at t = 5");
    check_close(h.normal.z, 1.0f, "normal on the arriving side");

    r.origin = hv3(3, 1, 5);
    check_int(holo_ray_rect(r, corner, eu, ev, &h), 0, "past the edge misses");

    r.origin = hv3(1, 1, 5);
    r.dir = hv3(1, 0, 0);
    check_int(holo_ray_rect(r, corner, eu, ev, &h), 0, "parallel misses");

    /* A skewed parallelogram: edges (2,0,0) and (1,2,0). The point
       (0.4, 1.0) is OUTSIDE it (affine u = -0.05), but projecting onto each
       edge independently would call it inside (u = 0.2) -- this is the case
       the Gram solve exists for. */
    ev = hv3(1, 2, 0);
    r.origin = hv3(0.4f, 1.0f, 5);
    r.dir = hv3(0, 0, -1);
    check_int(holo_ray_rect(r, corner, eu, ev, &h), 0,
              "skewed edges use true affine coords");
    /* And a point genuinely inside the skewed panel still hits. */
    r.origin = hv3(1.9f, 0.2f, 5);   /* u = 0.9, v = 0.1 */
    check_int(holo_ray_rect(r, corner, eu, ev, &h), 1, "inside the skew hits");
}

/* Distance from a point to the line (origin, dir). */
static float line_dist(HoloV3 point, HoloV3 origin, HoloV3 dir) {
    HoloV3 rel = hv3_sub(point, origin);
    HoloV3 perp = hv3_sub(rel, hv3_scale(dir, hv3_dot(rel, dir)));
    return hv3_len(perp);
}

static void test_dish_paraboloid(void) {
    printf("geometry: a paraboloid focuses at R/2, every zone\n");
    /* K = -1, R = 2: focal length exactly 1. Parallel rays at any radius
       must reflect through (0,0,1) -- the definition of a paraboloid, and
       the test that catches a wrong normal as surely as a wrong sag. */
    HoloV3 apex = hv3(0, 0, 0), axis = hv3(0, 0, 1), focus = hv3(0, 0, 1);
    float radii[] = { 0.3f, 0.8f, 1.4f };
    for (int i = 0; i < 3; i++) {
        HoloRay r = { .origin = hv3(radii[i], 0, 5), .dir = hv3(0, 0, -1) };
        HoloHit h;
        check_int(holo_ray_dish(r, apex, axis, 2.0f, -1.0f, 1.6f, &h), 1,
                  "parallel ray lands on the dish");
        check_close(h.point.z, radii[i] * radii[i] / 4.0f, "at the sag");
        HoloV3 out = hv3_reflect(r.dir, h.normal);
        check_close(line_dist(focus, h.point, out), 0.0f,
                    "reflection passes through the focus");
    }

    /* Straight down the axis: the degenerate quadratic, and the reflection
       comes straight back. */
    HoloRay r = { .origin = hv3(0, 0, 5), .dir = hv3(0, 0, -1) };
    HoloHit h;
    check_int(holo_ray_dish(r, apex, axis, 2.0f, -1.0f, 1.6f, &h), 1,
              "axial ray hits the vertex");
    check_close(h.t, 5.0f, "at the apex");

    /* Outside the rim there is no dish. */
    r.origin = hv3(2.5f, 0, 5);
    check_int(holo_ray_dish(r, apex, axis, 2.0f, -1.0f, 1.6f, &h), 0,
              "past the rim misses");
}

static void test_dish_ellipsoid(void) {
    printf("geometry: an ellipsoid images focus onto focus\n");
    /* a = 2, e = 0.5: R = b^2/a = 1.5, K = -e^2 = -0.25, foci at z = 1 and
       z = 3 from the vertex. A ray leaving the near focus must reflect
       through the far one -- the property whisper galleries and X-ray
       telescope tolerances both live on. */
    HoloV3 apex = hv3(0, 0, 0), axis = hv3(0, 0, 1);
    HoloV3 f2 = hv3(0, 0, 3);
    float angles[] = { 0.4f, 0.8f, 1.2f };
    for (int i = 0; i < 3; i++) {
        HoloRay r = { .origin = hv3(0, 0, 1),
                      .dir = hv3_norm(hv3(sinf(angles[i]), 0,
                                          -cosf(angles[i]))) };
        HoloHit h;
        check_int(holo_ray_dish(r, apex, axis, 1.5f, -0.25f, 1.55f, &h), 1,
                  "the ray from the near focus lands");
        HoloV3 out = hv3_reflect(r.dir, h.normal);
        check_close(line_dist(f2, h.point, out), 0.0f,
                    "and reflects through the far focus");
    }
}

static void test_dish_tilted(void) {
    printf("geometry: the dish frame is not the world frame\n");
    /* The same paraboloid pointed along +x: an x-parallel ray at height
       0.8 must still reflect through the (now rotated) focus. */
    HoloV3 apex = hv3(2, 1, 3), axis = hv3(1, 0, 0);
    HoloV3 focus = hv3(3, 1, 3);   /* apex + f * axis */
    HoloRay r = { .origin = hv3(8, 1.8f, 3), .dir = hv3(-1, 0, 0) };
    HoloHit h;
    check_int(holo_ray_dish(r, apex, axis, 2.0f, -1.0f, 1.6f, &h), 1,
              "hits the tilted dish");
    HoloV3 out = hv3_reflect(r.dir, h.normal);
    check_close(line_dist(focus, h.point, out), 0.0f,
                "the focus rides with the frame");
}

/* The hoisted basis must be the long form, exactly: same normal, same u and
   v, same hit or miss on every ray. If these two ever disagree, the GPU is
   intersecting a different rectangle than the oracle is. */
static void test_rect_basis(void) {
    printf("geometry: the hoisted rect basis is the long form\n");

    /* A deliberately skewed panel, so the Gram terms are not degenerate and
       an independent-projection shortcut would be caught out. */
    HoloV3 corner = hv3(-1, 0, 0);
    HoloV3 eu = hv3(2, 0, 0);
    HoloV3 ev = hv3(0.7f, 1.6f, 0);

    HoloV3 n, su, sv;
    holo_rect_basis(eu, ev, &n, &su, &sv);

    HoloV3 want_n = hv3_norm(hv3_cross(eu, ev));
    check_close(n.x, want_n.x, "basis normal x");
    check_close(n.y, want_n.y, "basis normal y");
    check_close(n.z, want_n.z, "basis normal z");
    check_close(hv3_dot(n, n), 1.0f, "basis normal is unit");

    /* solve_u and solve_v must read off the affine coordinates of a point
       built from known u and v. */
    float want_u = 0.3f, want_v = 0.8f;
    HoloV3 rel = hv3_add(hv3_scale(eu, want_u), hv3_scale(ev, want_v));
    check_close(hv3_dot(rel, su), want_u, "solve_u reads u back");
    check_close(hv3_dot(rel, sv), want_v, "solve_v reads v back");

    /* An edge vector alone must read as u=1,v=0 and u=0,v=1 -- the corners
       of the unit square the inside test compares against. */
    check_close(hv3_dot(eu, su), 1.0f, "edge_u is u=1");
    check_close(hv3_dot(eu, sv), 0.0f, "edge_u is v=0");
    check_close(hv3_dot(ev, su), 0.0f, "edge_v is u=0");
    check_close(hv3_dot(ev, sv), 1.0f, "edge_v is v=1");

    /* And the two intersectors must agree ray for ray, hits and misses
       alike, including rays that graze just outside the edges. */
    for (int i = 0; i < 25; i++) {
        float fu = -0.2f + 0.06f * (float)(i % 5) * 5.0f;
        float fv = -0.2f + 0.06f * (float)(i / 5) * 5.0f;
        HoloV3 target = hv3_add(corner,
                                hv3_add(hv3_scale(eu, fu), hv3_scale(ev, fv)));
        HoloV3 origin = hv3(target.x, target.y, 4.0f);
        HoloRay r = { origin, hv3_norm(hv3_sub(target, origin)) };

        HoloHit a, b;
        int hit_a = holo_ray_rect(r, corner, eu, ev, &a);
        int hit_b = holo_ray_rect_pre(r, corner, n, su, sv, &b);
        check_int(hit_b, hit_a, "hoisted and long form agree on hit or miss");
        if (hit_a && hit_b) {
            check_close(b.t, a.t, "hoisted and long form agree on t");
            check_close(b.normal.z, a.normal.z, "and on the facing normal");
        }
    }
}

int main(void) {
    test_sphere();
    test_plane();
    test_rect();
    test_rect_basis();
    test_dish_paraboloid();
    test_dish_ellipsoid();
    test_dish_tilted();
    return report();
}
