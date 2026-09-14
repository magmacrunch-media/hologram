/* See geometry.h. Closed forms only; the tests hold each one to the algebra. */
#include <math.h>
#include "geometry.h"

int holo_ray_sphere(HoloRay r, HoloV3 center, float radius, HoloHit *hit) {
    /* |o + t d - c|^2 = R^2, quadratic in t. With d unit, a = 1. */
    HoloV3 oc = hv3_sub(r.origin, center);
    float b = hv3_dot(oc, r.dir);
    float c = hv3_dot(oc, oc) - radius * radius;
    float disc = b * b - c;
    if (disc < 0.0f) {
        return 0;
    }
    float sq = sqrtf(disc);
    /* Nearer root first; the farther one still counts when the origin is
       inside the sphere (that is the exit through the far wall, and glass
       will need it). */
    float t = -b - sq;
    if (t <= HOLO_T_MIN) {
        t = -b + sq;
    }
    if (t <= HOLO_T_MIN) {
        return 0;
    }
    hit->t = t;
    hit->point = hv3_add(r.origin, hv3_scale(r.dir, t));
    /* Out of the surface on the arriving side: flipped when we hit the far
       wall from inside. */
    HoloV3 n = hv3_scale(hv3_sub(hit->point, center), 1.0f / radius);
    hit->normal = hv3_dot(n, r.dir) < 0.0f ? n : hv3_scale(n, -1.0f);
    return 1;
}

void holo_rect_basis(HoloV3 edge_u, HoloV3 edge_v,
                     HoloV3 *normal, HoloV3 *solve_u, HoloV3 *solve_v) {
    /* The Gram matrix of the two edges. Edges need not be perpendicular, so
       this is really a parallelogram and independent projections would not
       do -- every mirror so far is a rectangle, and the math does not care. */
    float uu = hv3_dot(edge_u, edge_u), vv = hv3_dot(edge_v, edge_v);
    float uv = hv3_dot(edge_u, edge_v);
    float inv_det = 1.0f / (uu * vv - uv * uv);
    /* u = (ru*vv - rv*uv)/det, and ru is dot(rel, edge_u), so the whole
       solve collapses into one vector per coordinate. */
    *solve_u = hv3_scale(hv3_sub(hv3_scale(edge_u, vv),
                                 hv3_scale(edge_v, uv)), inv_det);
    *solve_v = hv3_scale(hv3_sub(hv3_scale(edge_v, uu),
                                 hv3_scale(edge_u, uv)), inv_det);
    /* The normal comes back out of the solve vectors rather than the edges:
       both lie in the panel's plane and their cross is (edge_u x edge_v)/det
       with det positive, so this is the same direction, exactly.

       It is derived rather than stored because the shaders derive it too.
       Shipping a third vector per panel would cost an indexed uniform fetch
       in the innermost loop, and measurement says that fetch is dearer than
       the cross and normalize it would save: 15.4ms against 11.4ms at 64
       panels. Deriving it here keeps the oracle and the GPU on identical
       arithmetic. */
    *normal = hv3_norm(hv3_cross(*solve_u, *solve_v));
}

int holo_ray_rect_pre(HoloRay r, HoloV3 corner, HoloV3 normal,
                      HoloV3 solve_u, HoloV3 solve_v, HoloHit *hit) {
    /* Meet the rectangle's plane first, then ask whether the point landed
       inside the edges. */
    HoloHit h;
    if (!holo_ray_plane(r, corner, normal, &h)) {
        return 0;
    }
    HoloV3 rel = hv3_sub(h.point, corner);
    float u = hv3_dot(rel, solve_u);
    float v = hv3_dot(rel, solve_v);
    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) {
        return 0;
    }
    *hit = h;
    return 1;
}

int holo_ray_rect(HoloRay r, HoloV3 corner, HoloV3 edge_u, HoloV3 edge_v,
                  HoloHit *hit) {
    HoloV3 n, su, sv;
    holo_rect_basis(edge_u, edge_v, &n, &su, &sv);
    return holo_ray_rect_pre(r, corner, n, su, sv, hit);
}

void holo_basis(HoloV3 axis, HoloV3 *u, HoloV3 *v) {
    HoloV3 helper = fabsf(axis.x) > 0.9f ? hv3(0, 1, 0) : hv3(1, 0, 0);
    *u = hv3_norm(hv3_cross(helper, axis));
    *v = hv3_cross(axis, *u);
}

int holo_ray_dish(HoloRay r, HoloV3 apex, HoloV3 axis,
                  float curv_r, float conic_k, float rim, HoloHit *hit) {
    /* Into the dish's frame: z along the axis, apex at the origin. The
       surface is x^2 + y^2 + (1+K) z^2 - 2Rz = 0 -- the conic sag equation
       cleared of its square root -- which is quadratic along the ray. */
    HoloV3 u, v;
    holo_basis(axis, &u, &v);
    HoloV3 rel = hv3_sub(r.origin, apex);
    HoloV3 o = hv3(hv3_dot(rel, u), hv3_dot(rel, v), hv3_dot(rel, axis));
    HoloV3 d = hv3(hv3_dot(r.dir, u), hv3_dot(r.dir, v), hv3_dot(r.dir, axis));

    float p = 1.0f + conic_k;
    float A = d.x * d.x + d.y * d.y + p * d.z * d.z;
    float B = o.x * d.x + o.y * d.y + p * o.z * d.z - curv_r * d.z;
    float C = o.x * o.x + o.y * o.y + p * o.z * o.z - 2.0f * curv_r * o.z;

    /* The cap ends at the rim's sag; anything past it (an ellipsoid's far
       half, a hyperboloid's other sheet) is not part of the dish. */
    float rr = rim * rim;
    float root = 1.0f - p * rr / (curv_r * curv_r);
    float z_max = rr / (curv_r * (1.0f + sqrtf(root > 0.0f ? root : 0.0f)));

    float t1, t2;
    if (fabsf(A) < 1e-8f) {
        /* An axis-parallel ray on a paraboloid: the quadratic degenerates
           to a line, one crossing. */
        if (fabsf(B) < 1e-12f) {
            return 0;
        }
        t1 = -C / (2.0f * B);
        t2 = -1.0f;
    } else {
        float disc = B * B - A * C;
        if (disc < 0.0f) {
            return 0;
        }
        float sq = sqrtf(disc);
        t1 = (-B - sq) / A;
        t2 = (-B + sq) / A;
    }

    /* Nearest crossing that is in front of the ray AND on the cap: the
       near root can be a clipped sheet while the far one is the visible
       bowl (looking into a concave mirror does exactly this). */
    for (int pass = 0; pass < 2; pass++) {
        float t = pass == 0 ? t1 : t2;
        if (t <= HOLO_T_MIN) {
            continue;
        }
        float z = o.z + t * d.z;
        float x = o.x + t * d.x;
        float y = o.y + t * d.y;
        if (z < 0.0f || z > z_max || x * x + y * y > rr) {
            continue;
        }
        hit->t = t;
        hit->point = hv3_add(r.origin, hv3_scale(r.dir, t));
        /* Gradient of the surface equation, back in world axes. */
        HoloV3 n_local = hv3(x, y, p * z - curv_r);
        HoloV3 n = hv3_norm(hv3_add(hv3_add(hv3_scale(u, n_local.x),
                                            hv3_scale(v, n_local.y)),
                                    hv3_scale(axis, n_local.z)));
        hit->normal = hv3_dot(n, r.dir) < 0.0f ? n : hv3_scale(n, -1.0f);
        return 1;
    }
    return 0;
}

float holo_fresnel_tilt(float r, float focal, float n_design) {
    /* tan a = sin d / (n - cos d), with sin d = r/h and cos d = f/h for h
       the hypotenuse of (r, f): the h cancels into atan2(r, n h - f). The
       denominator is positive for any n > 1 since h >= f, so the angle
       lands in [0, pi/2) and the axis itself reads as a flat facet. */
    float h = sqrtf(r * r + focal * focal);
    return atan2f(r, n_design * h - focal);
}

/* The pieces of a Fresnel lens, in its own frame (z along the axis, the
   ring peaks on z = 0). Every one is a surface of revolution, so each takes
   the ray's radial quadratic |xy(t)|^2 = P0 + 2 P1 t + P2 t^2 ready-made
   and competes for the nearest t: the winner's t and local normal come
   back through best and best_n, untouched on a miss. */

/* A cylinder rho = R between two heights: a riser, or the rim wall. */
static void fresnel_cylinder(float P0, float P1, float P2, HoloV3 o, HoloV3 d,
                             float R, float z_lo, float z_hi,
                             float *best, HoloV3 *best_n) {
    if (P2 < 1e-12f) {
        return;   /* along the axis: parallel to every cylinder */
    }
    float disc = P1 * P1 - P2 * (P0 - R * R);
    if (disc < 0.0f) {
        return;
    }
    float sq = sqrtf(disc);
    for (int pass = 0; pass < 2; pass++) {
        float t = (pass == 0 ? -P1 - sq : -P1 + sq) / P2;
        float z = o.z + t * d.z;
        if (t > HOLO_T_MIN && t < *best && z >= z_lo && z <= z_hi) {
            *best = t;
            *best_n = hv3((o.x + t * d.x) / R, (o.y + t * d.y) / R, 0.0f);
        }
    }
}

/* One ring's facet: the cone rho sin a + z cos a = r_k sin a, which has
   its peak at (r_k, 0) and descends outward at tilt a. Squared for the
   quadratic, s^2 |xy|^2 = q^2 with q = r_k s - z cos a, the mirror sheet
   being q < 0. Clipped to the ring's span in radius, which is what decides
   a corner between one ring and the next: the seed only says where to
   look. At a = 0 the cone is the plane z = 0 and the algebra still holds,
   which is how the central disc of a lens whose rings start on the axis
   comes out flat without a special case. */
static void fresnel_facet(float P0, float P1, float P2, HoloV3 o, HoloV3 d,
                          float rk, float r_out, float s, float cs,
                          float *best, HoloV3 *best_n) {
    float q0 = rk * s - o.z * cs;
    float qd = -d.z * cs;
    float A = s * s * P2 - qd * qd;
    float B = s * s * P1 - q0 * qd;
    float C = s * s * P0 - q0 * q0;
    float t1, t2;
    if (fabsf(A) < 1e-10f) {
        if (fabsf(B) < 1e-12f) {
            return;
        }
        t1 = -C / (2.0f * B);
        t2 = -1.0f;
    } else {
        float disc = B * B - A * C;
        if (disc < 0.0f) {
            return;
        }
        float sq = sqrtf(disc);
        t1 = (-B - sq) / A;
        t2 = (-B + sq) / A;
    }
    for (int pass = 0; pass < 2; pass++) {
        float t = pass == 0 ? t1 : t2;
        float x = o.x + t * d.x, y = o.y + t * d.y, z = o.z + t * d.z;
        float rho = sqrtf(x * x + y * y);
        float q = rk * s - z * cs;
        /* The sheet test carries a hair of slack for the flat disc, where
           q is exactly zero on the surface and float noise puts it either
           side; the mirror sheet of a real cone sits at q = -rho s, far
           beyond it. */
        if (t > HOLO_T_MIN && t < *best && q > -1e-6f &&
            rho >= rk && rho <= r_out) {
            *best = t;
            /* The gradient of rho sin a + z cos a is unit already and points
               out of the glass. */
            float inv = rho > 1e-12f ? 1.0f / rho : 0.0f;
            *best_n = hv3(s * x * inv, s * y * inv, cs);
        }
    }
}

int holo_ray_fresnel(HoloRay r, HoloV3 center, HoloV3 axis,
                     float focal, float n_design, float r0, float pitch,
                     float rim, float thick, HoloHit *hit) {
    HoloV3 u, v;
    holo_basis(axis, &u, &v);
    HoloV3 rel = hv3_sub(r.origin, center);
    HoloV3 o = hv3(hv3_dot(rel, u), hv3_dot(rel, v), hv3_dot(rel, axis));
    HoloV3 d = hv3(hv3_dot(r.dir, u), hv3_dot(r.dir, v), hv3_dot(r.dir, axis));

    float P2 = d.x * d.x + d.y * d.y;
    float P1 = o.x * d.x + o.y * d.y;
    float P0 = o.x * o.x + o.y * o.y;
    float rr = rim * rim;

    /* The bounding volume: the slab -thick <= z <= 0 inside rho <= rim.
       Most rays in a frame never enter it and are done here; the rest
       leave with the t at which they do, which seeds the ring search. */
    float lo = 0.0f, hi = 1e30f;
    if (fabsf(d.z) < 1e-8f) {
        if (o.z < -thick || o.z > 0.0f) {
            return 0;
        }
    } else {
        float ta = (-thick - o.z) / d.z, tb = -o.z / d.z;
        if (ta > tb) {
            float swap = ta;
            ta = tb;
            tb = swap;
        }
        if (ta > lo) lo = ta;
        if (tb < hi) hi = tb;
    }
    if (P2 < 1e-12f) {
        if (P0 > rr) {
            return 0;
        }
    } else {
        float disc = P1 * P1 - P2 * (P0 - rr);
        if (disc < 0.0f) {
            return 0;
        }
        float sq = sqrtf(disc);
        float ta = (-P1 - sq) / P2, tb = (-P1 + sq) / P2;
        if (ta > lo) lo = ta;
        if (tb < hi) hi = tb;
    }
    if (hi < lo || hi <= HOLO_T_MIN) {
        return 0;
    }

    /* A hair off the quotient so a rim that lands exactly on a ring edge
       does not conjure a zero-width ring past it. */
    int rings = (int)ceilf((rim - r0) / pitch - 1e-4f);
    if (rings < 1) {
        return 0;
    }

    float best = 1e30f;
    HoloV3 best_n = hv3(0, 0, 1);

    /* The back face: the plane z = -thick, an annulus from r0 to rim. */
    if (fabsf(d.z) > 1e-8f) {
        float t = (-thick - o.z) / d.z;
        float q = P0 + 2.0f * P1 * t + P2 * t * t;
        if (t > HOLO_T_MIN && q <= rr && q >= r0 * r0) {
            best = t;
            best_n = hv3(0, 0, -1);
        }
    }

    /* The walls. The outer one runs from the back up to where the last
       ring's facet meets the rim -- not to z = 0, or the wall would stand
       proud of the glass in the groove it closes off. */
    {
        float rk = r0 + (float)(rings - 1) * pitch;
        float a = holo_fresnel_tilt(rk, focal, n_design);
        float top = -(rim - rk) * tanf(a);
        fresnel_cylinder(P0, P1, P2, o, d, rim, -thick, top, &best, &best_n);
        if (r0 > 0.0f) {
            fresnel_cylinder(P0, P1, P2, o, d, r0, -thick, 0.0f, &best, &best_n);
        }
    }

    /* The rings, a fixed window either side of the one the ray enters
       the slab over. floor() seeds; each ring's own span decides. */
    float rho_seed = sqrtf(P0 + 2.0f * P1 * lo + P2 * lo * lo);
    int k_seed = (int)floorf((rho_seed - r0) / pitch);
    if (k_seed < 0) k_seed = 0;
    if (k_seed > rings - 1) k_seed = rings - 1;
    for (int i = -HOLO_FRESNEL_WINDOW; i <= HOLO_FRESNEL_WINDOW; i++) {
        int k = k_seed + i;
        if (k >= 0 && k < rings) {
            float rk = r0 + (float)k * pitch;
            float r_out = rk + pitch < rim ? rk + pitch : rim;
            float a = holo_fresnel_tilt(rk, focal, n_design);
            float s = sinf(a), cs = cosf(a);
            fresnel_facet(P0, P1, P2, o, d, rk, r_out, s, cs, &best, &best_n);
            if (k < rings - 1) {
                /* The riser at the outer edge, from this ring's valley up
                   to the next ring's peak. */
                float depth = pitch * s / cs;
                fresnel_cylinder(P0, P1, P2, o, d, rk + pitch, -depth, 0.0f,
                                 &best, &best_n);
            }
        }
    }

    if (best >= 1e29f) {
        return 0;
    }
    hit->t = best;
    hit->point = hv3_add(r.origin, hv3_scale(r.dir, best));
    HoloV3 n = hv3_add(hv3_add(hv3_scale(u, best_n.x), hv3_scale(v, best_n.y)),
                       hv3_scale(axis, best_n.z));
    hit->normal = hv3_dot(n, r.dir) < 0.0f ? n : hv3_scale(n, -1.0f);
    return 1;
}

int holo_ray_plane(HoloRay r, HoloV3 point, HoloV3 normal, HoloHit *hit) {
    float denom = hv3_dot(normal, r.dir);
    if (fabsf(denom) < 1e-8f) {
        return 0;   /* parallel: no crossing, or infinitely many */
    }
    float t = hv3_dot(hv3_sub(point, r.origin), normal) / denom;
    if (t <= HOLO_T_MIN) {
        return 0;
    }
    hit->t = t;
    hit->point = hv3_add(r.origin, hv3_scale(r.dir, t));
    hit->normal = denom < 0.0f ? normal : hv3_scale(normal, -1.0f);
    return 1;
}
