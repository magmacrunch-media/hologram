/* The camera and the oracle. The camera's rays are held to the geometry of
 * looking (the center pixel looks exactly at the target); the tracer's
 * colors to Lambert under one sun: a surface facing the sun shows its full
 * albedo, a shadowed one exactly HOLO_AMBIENT of it, and a ray that misses
 * everything sees the sky the M0 shader drew.
 *
 *   build.bat test
 */
#include <math.h>
#include <stdio.h>
#include "harness.h"
#include "cpu_trace.h"

static void test_camera(void) {
    printf("trace: camera as ray generator\n");
    HoloCamera cam = holo_camera_make(hv3(0, 0, 5), hv3(0, 0, 0), hv3(0, 1, 0),
                                      60.0f, 4.0f / 3.0f);

    /* The center pixel looks straight at the target. */
    HoloRay r = holo_camera_ray(&cam, 0.5f, 0.5f);
    check_close(r.dir.x, 0, "center ray x");
    check_close(r.dir.y, 0, "center ray y");
    check_close(r.dir.z, -1, "center ray toward the target");
    check_close(hv3_len(r.dir), 1.0f, "rays are unit");

    /* The top edge of the frame sits half the vertical fov up. */
    r = holo_camera_ray(&cam, 0.5f, 0.0f);
    check_close(atanf(r.dir.y / -r.dir.z), 30.0f * 3.14159265f / 180, "half fov to the top edge");

    /* v runs top to bottom, so the frame's vertical mirror flips y. */
    HoloRay top = holo_camera_ray(&cam, 0.25f, 0.0f);
    HoloRay bot = holo_camera_ray(&cam, 0.25f, 1.0f);
    check_close(top.dir.y, -bot.dir.y, "top and bottom mirror");
    check_close(top.dir.x, bot.dir.x, "without touching x");
}

static void test_shading(void) {
    printf("trace: Lambert under one sun\n");
    HoloScene scene = {
        .spheres = { { .center = hv3(0, 1.5f, 0), .radius = 1,
                       .albedo = hv3(0.8f, 0.2f, 0.4f) } },
        .sphere_count = 1,
        .has_floor = 1,
        .floor_y = 0,
        .floor_a = hv3(0.9f, 0.9f, 0.9f),
        .floor_b = hv3(0.1f, 0.1f, 0.1f),
        .sun_dir = hv3(0, 1, 0),   /* noon: straight up */
        .horizon = hv3(1, 0.9f, 0.8f),
        .zenith  = hv3(0.25f, 0.45f, 0.9f),
    };

    /* The sphere's north pole faces the noon sun head on: full albedo. */
    HoloRay r = { .origin = hv3(0, 5, 0), .dir = hv3(0, -1, 0) };
    HoloV3 c = holo_trace_ray(&scene, r);
    check_close(c.x, 0.8f, "lit pole shows full albedo r");
    check_close(c.y, 0.2f, "g");
    check_close(c.z, 0.4f, "b");

    /* Far from the sphere the floor faces the sun too -- full checker
       color, and (10.5, 10.5) has even cell parity: floor_a. */
    r.origin = hv3(10.5f, 5, 10.5f);
    c = holo_trace_ray(&scene, r);
    check_close(c.x, 0.9f, "open floor fully lit");

    /* One cell over the parity flips. */
    r.origin = hv3(11.5f, 5, 10.5f);
    c = holo_trace_ray(&scene, r);
    check_close(c.x, 0.1f, "next cell is the other color");

    /* Under the sphere the sun is blocked: exactly the ambient floor. */
    HoloRay under = { .origin = hv3(0.1f, 0.5f, 0), .dir = hv3(0, -1, 0) };
    c = holo_trace_ray(&scene, under);
    check_close(c.x, 0.9f * HOLO_AMBIENT, "shadowed floor is ambient only");

    /* A ray past everything sees the sky: straight up is the zenith. */
    HoloRay up = { .origin = hv3(50, 1, 0), .dir = hv3(0, 1, 0) };
    c = holo_trace_ray(&scene, up);
    check_close(c.z, 0.9f, "straight up is the zenith");
}

static void test_image(void) {
    printf("trace: a small frame end to end\n");
    HoloScene scene = {
        .sphere_count = 0,
        .has_floor = 0,
        .sun_dir = hv3(0, 1, 0),
        .horizon = hv3(1, 1, 1),
        .zenith  = hv3(0, 0, 0),
    };
    HoloCamera cam = holo_camera_make(hv3(0, 0, 5), hv3(0, 0, 0), hv3(0, 1, 0),
                                      60.0f, 1.0f);
    float rgb[4 * 4 * 3];
    holo_trace_image(&scene, &cam, 4, 4, rgb);

    /* Pure sky gradient: the top row looks up (darker), the bottom row
       looks down (brighter), columns identical. */
    check(rgb[0] < rgb[3 * 12], "sky darkens upward");
    check_close(rgb[0], rgb[3 * 3], "and is flat across a row");
}

int main(void) {
    test_camera();
    test_shading();
    test_image();
    return report();
}
