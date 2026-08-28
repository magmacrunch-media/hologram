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

int holo_ray_rect(HoloRay r, HoloV3 corner, HoloV3 edge_u, HoloV3 edge_v,
                  HoloHit *hit) {
    /* Meet the rectangle's plane first, then ask whether the point landed
       inside the edges. The edge test projects the landing onto each edge;
       edges need not be perpendicular, so this is really a parallelogram --
       every mirror so far is a rectangle, and the math does not care. */
    HoloV3 n = hv3_norm(hv3_cross(edge_u, edge_v));
    HoloHit h;
    if (!holo_ray_plane(r, corner, n, &h)) {
        return 0;
    }
    /* rel = u*edge_u + v*edge_v, solved through the Gram matrix so skewed
       edges get true affine coordinates (independent projections would only
       be right for perpendicular edges). */
    HoloV3 rel = hv3_sub(h.point, corner);
    float uu = hv3_dot(edge_u, edge_u), vv = hv3_dot(edge_v, edge_v);
    float uv = hv3_dot(edge_u, edge_v);
    float ru = hv3_dot(rel, edge_u), rv = hv3_dot(rel, edge_v);
    float det = uu * vv - uv * uv;
    float u = (ru * vv - rv * uv) / det;
    float v = (rv * uu - ru * uv) / det;
    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) {
        return 0;
    }
    *hit = h;
    return 1;
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
