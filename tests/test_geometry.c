/* Rays against the analytic surfaces, held to cases solvable in your head:
 * a ray 5m from a unit sphere hits it at t = 4, an inside ray finds the far
 * wall with the normal flipped to meet it, a parallel ray never finds a
 * plane at all.
 *
 *   build.bat test
 */
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

int main(void) {
    test_sphere();
    test_plane();
    return report();
}
