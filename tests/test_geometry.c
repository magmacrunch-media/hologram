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

static void test_fresnel_tilt(void) {
    printf("geometry: a Fresnel ring's tilt is the prism equation, solved\n");
    /* f = 0.7 m, n = 1.5. The tilt returned must satisfy the equation it
       claims to solve, arcsin(n sin a) - a = atan(r/f), at every radius: a
       wrong branch or a dropped term shows up as a deviation that is not
       the one asked for. */
    float f = 0.7f, n = 1.5f;
    float ratios[] = { 0.1f, 0.3f, 0.6f, 1.0f };
    for (int i = 0; i < 4; i++) {
        float r = ratios[i] * f;
        float a = holo_fresnel_tilt(r, f, n);
        float dev = asinf(n * sinf(a)) - a;
        check_close(dev, atanf(r / f), "deviation is atan(r/f)");
    }

    /* On the axis there is nothing to deviate and the facet is flat. */
    check_close(holo_fresnel_tilt(0.0f, f, n), 0.0f, "axial ring is flat");

    /* r << f is the thin prism of every optics course, delta = (n - 1) A,
       so A = (r/f)/(n-1). */
    float r_thin = 0.001f * f;
    check_close(holo_fresnel_tilt(r_thin, f, n), (r_thin / f) / (n - 1.0f),
                "thin-prism limit");
}

static void test_fresnel_tilt_critical(void) {
    printf("geometry: the outermost usable ring is at the critical angle\n");
    /* The tilt grows with radius until n sin a = 1 -- the exit ray grazes
       the facet -- and that happens at exactly r = f sqrt(n^2 - 1). For
       n = 1.5 the angle there is asin(1/1.5) = 41.81 degrees, the same
       number test_linalg pins for total internal reflection: the lens's
       design limit and Snell's limit are one fact. */
    float f = 0.7f, n = 1.5f;
    float r_max = f * sqrtf(n * n - 1.0f);
    float a = holo_fresnel_tilt(r_max, f, n);
    check_close(a, asinf(1.0f / n), "tilt at r_max is the critical angle");
    check_close(a, 41.8103f * 3.14159265f / 180.0f, "which is 41.81 degrees");
    check_close(n * sinf(a), 1.0f, "and the exit ray grazes");

    /* Just inside the limit the ring still transmits: n sin a < 1. */
    check(n * sinf(holo_fresnel_tilt(0.9f * r_max, f, n)) < 1.0f,
          "inside the limit the facet still transmits");
}

/* The lens the Fresnel tests share: f = 0.7 m, n = 1.5, rings 30 mm wide
   out to 0.6 m -- inside f sqrt(n^2 - 1) = 0.78 m, so every ring
   transmits -- in 50 mm of slab, which clears the deepest groove at 26 mm.
   Twenty rings; ring k peaks at 0.03 k. */
#define LENS_F 0.7f
#define LENS_N 1.5f
#define LENS_PITCH 0.03f
#define LENS_RIM 0.6f
#define LENS_THICK 0.05f

static int lens_hit(HoloRay r, HoloHit *h) {
    return holo_ray_fresnel(r, hv3(0, 0, 0), hv3(0, 0, 1), LENS_F, LENS_N,
                            0.0f, LENS_PITCH, LENS_RIM, LENS_THICK, h);
}

static float ring_tilt(int k) {
    return holo_fresnel_tilt((float)k * LENS_PITCH, LENS_F, LENS_N);
}

static void test_fresnel_sag_and_normal(void) {
    printf("geometry: a Fresnel facet sags at its tilt and faces at it\n");
    /* Straight down onto the middle of ring 5: the facet peaks at 0.15 on
       z = 0 and descends at tan(a5), so 15 mm further out it sits at
       -0.015 tan(a5), and the normal is the axis leaned outward by a5. */
    float a5 = ring_tilt(5);
    HoloRay r = { .origin = hv3(0.165f, 0, 1), .dir = hv3(0, 0, -1) };
    HoloHit h;
    check_int(lens_hit(r, &h), 1, "lands on ring 5");
    check_close(h.point.z, -0.015f * tanf(a5), "at the facet's sag");
    check_close(h.normal.z, cosf(a5), "normal leans by the tilt");
    check_close(h.normal.x, sinf(a5), "outward, radially");
    check_close(h.normal.y, 0.0f, "and nowhere else");

    /* Ring 12, near its outer edge: steeper, deeper. */
    float a12 = ring_tilt(12);
    r.origin = hv3(0.385f, 0, 1);
    check_int(lens_hit(r, &h), 1, "lands on ring 12");
    check_close(h.point.z, -0.025f * tanf(a12), "at ring 12's sag");
    check_close(h.normal.z, cosf(a12), "and ring 12's tilt");

    /* The central disc, whose tilt is zero, is flat and faces the axis. */
    r.origin = hv3(0.01f, 0, 1);
    check_int(lens_hit(r, &h), 1, "lands on the centre");
    check_close(h.point.z, 0.0f, "which is flat");
    check_close(h.normal.z, 1.0f, "and faces straight up");
}

static void test_fresnel_collimates(void) {
    printf("geometry: from the focus, a ring's facet sends light down the axis\n");
    /* A ring's tilt is designed at its peak radius r_k, so a ray from the
       focus meeting the facet just outside the peak must refract to run
       along the axis inside the glass -- the lighthouse, run backwards
       from the lamp. Just outside rather than at the peak, because the
       peak is the corner the previous riser shares. The 1.5 mm offset
       misses exact by the ring's own angular width, which the checks
       allow for and no more. */
    int ks[] = { 3, 5, 12 };
    HoloV3 focus = hv3(0, 0, LENS_F), axis = hv3(0, 0, 1);
    float off = 0.05f * LENS_PITCH;
    for (int i = 0; i < 3; i++) {
        float rk = (float)ks[i] * LENS_PITCH;
        float a = ring_tilt(ks[i]);
        HoloV3 target = hv3(rk + off, 0, -off * tanf(a));
        HoloRay r = { .origin = focus, .dir = hv3_norm(hv3_sub(target, focus)) };
        HoloHit h;
        check_int(lens_hit(r, &h), 1, "the ray from the focus lands");
        check_close(h.point.x, rk + off, "on the ring it was aimed at");
        HoloV3 in;
        check_int(hv3_refract(r.dir, h.normal, 1.0f / LENS_N, &in), 1,
                  "and refracts in");
        check_close(hv3_dot(in, axis), -1.0f, "running down the axis");
        HoloV3 perp = hv3_sub(in, hv3_scale(axis, hv3_dot(in, axis)));
        check(hv3_len(perp) < 2.0f * off / LENS_F,
              "to within the ring's angular width");
    }

    /* The same facet from the wrong distance does not: a lens has one
       focus, and the tilts are a statement about where it is. */
    float a5 = ring_tilt(5);
    HoloV3 target = hv3(5 * LENS_PITCH + off, 0, -off * tanf(a5));
    HoloV3 wrong = hv3(0, 0, 1.5f * LENS_F);
    HoloRay r = { .origin = wrong, .dir = hv3_norm(hv3_sub(target, wrong)) };
    HoloHit h;
    check_int(lens_hit(r, &h), 1, "lands from the wrong focus too");
    HoloV3 in;
    hv3_refract(r.dir, h.normal, 1.0f / LENS_N, &in);
    HoloV3 perp = hv3_sub(in, hv3_scale(axis, hv3_dot(in, axis)));
    check(hv3_len(perp) > 0.01f, "but does not run down the axis");
}

static void test_fresnel_oblique_skips_a_ring(void) {
    printf("geometry: a ray inside the glass surfaces on a ring it did not start under\n");
    /* From 40 mm down inside the slab, over ring 5, heading out and up at
       45 degrees: it stays under ring 5's groove, passes beneath the riser
       at 0.18, and surfaces through ring 6's facet near 0.195. The seed
       says ring 5; the window is what finds ring 6. A single-ring lookup
       would report a miss and leave the tracer's inside flag stranded. */
    float a6 = ring_tilt(6);
    HoloRay r = { .origin = hv3(0.165f, 0, -0.04f),
                  .dir = hv3_norm(hv3(1, 0, 1)) };
    HoloHit h;
    check_int(lens_hit(r, &h), 1, "surfaces");
    check(h.point.x > 0.18f && h.point.x < 0.21f, "through ring 6");
    check_close(h.normal.z, -cosf(a6), "on ring 6's facet, from inside");
    check_close(h.normal.x, -sinf(a6), "normal turned to meet the ray");
}

static void test_fresnel_walls_and_back(void) {
    printf("geometry: the lens is a closed solid\n");
    float a19 = ring_tilt(19);
    HoloHit h;

    /* From behind: the flat back face, facing the ray. */
    HoloRay r = { .origin = hv3(0.2f, 0, -1), .dir = hv3(0, 0, 1) };
    check_int(lens_hit(r, &h), 1, "the back face");
    check_close(h.t, 1.0f - LENS_THICK, "at the slab's depth");
    check_close(h.normal.z, -1.0f, "faces out the back");

    /* From the side, low: the rim wall, radial normal. */
    r.origin = hv3(2, 0, -0.045f);
    r.dir = hv3(-1, 0, 0);
    check_int(lens_hit(r, &h), 1, "the rim wall");
    check_close(h.point.x, LENS_RIM, "at the rim");
    check_close(h.normal.x, 1.0f, "radially out");

    /* From the side, high: above where the last facet meets the rim there
       is no wall, and the ray flies in over the groove to meet the facet
       of ring 19 where it has risen to the ray's height. That is a ring
       found from a side entry, seeded at the rim. */
    r.origin = hv3(2, 0, -0.01f);
    check_int(lens_hit(r, &h), 1, "over the wall onto the last facet");
    check_close(h.point.x, 19 * LENS_PITCH + 0.01f / tanf(a19),
                "where the facet reaches the ray's height");
    check_close(h.normal.z, cosf(a19), "on ring 19's facet");

    /* Past the rim, and receding from behind: nothing. */
    r.origin = hv3(0.7f, 0, 1);
    r.dir = hv3(0, 0, -1);
    check_int(lens_hit(r, &h), 0, "past the rim misses");
    r.origin = hv3(0.2f, 0, -1);
    check_int(lens_hit(r, &h), 0, "receding misses");
}

static void test_fresnel_is_watertight(void) {
    printf("geometry: every ray through the lens comes out again\n");
    /* Straight down at 39 radii chosen to sit at least 7.5 mm from any
       ring edge: each must meet exactly one facet and then the back face
       -- two crossings, the second at the slab's depth. An odd count is a
       hole in the solid, and the glass walk would never toggle back out. */
    for (int i = 0; i < 39; i++) {
        float rho = 0.0075f + 0.015f * (float)i;
        HoloRay r = { .origin = hv3(rho, 0, 1), .dir = hv3(0, 0, -1) };
        HoloHit h;
        int crossings = 0;
        float last_z = 1.0f;
        while (crossings < 8 && lens_hit(r, &h)) {
            crossings++;
            last_z = h.point.z;
            r.origin = h.point;
        }
        check_int(crossings, 2, "in through a facet, out through the back");
        check_close(last_z, -LENS_THICK, "and the exit is the back face");
    }
}

static void test_fresnel_tilted(void) {
    printf("geometry: the lens frame is not the world frame\n");
    /* The ring-5 sag test again, with the lens at (2,1,3) pointing along
       +x: a ray coming back along -x at 0.165 off the axis must land at
       the same sag, now measured along x, with the normal leaned toward
       +y, which is outward here. */
    float a5 = ring_tilt(5);
    HoloRay r = { .origin = hv3(5, 1.165f, 3), .dir = hv3(-1, 0, 0) };
    HoloHit h;
    check_int(holo_ray_fresnel(r, hv3(2, 1, 3), hv3(1, 0, 0), LENS_F, LENS_N,
                               0.0f, LENS_PITCH, LENS_RIM, LENS_THICK, &h),
              1, "lands on the tilted lens");
    check_close(h.point.x, 2.0f - 0.015f * tanf(a5), "at the sag, along x");
    check_close(h.normal.x, cosf(a5), "normal along the axis by cos");
    check_close(h.normal.y, sinf(a5), "and outward by sin");
}

int main(void) {
    test_sphere();
    test_plane();
    test_rect();
    test_rect_basis();
    test_dish_paraboloid();
    test_dish_ellipsoid();
    test_dish_tilted();
    test_fresnel_tilt();
    test_fresnel_tilt_critical();
    test_fresnel_sag_and_normal();
    test_fresnel_collimates();
    test_fresnel_oblique_skips_a_ring();
    test_fresnel_walls_and_back();
    test_fresnel_is_watertight();
    test_fresnel_tilted();
    return report();
}
