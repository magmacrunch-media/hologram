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
