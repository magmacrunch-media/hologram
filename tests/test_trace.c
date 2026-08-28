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

static void test_mirror_image(void) {
    printf("trace: a mirror shows the mirrored scene\n");
    /* The oldest fact about mirrors: looking THROUGH one at a sphere must
       equal looking STRAIGHT at the sphere's mirror image. Scene A has a
       perfect wall mirror at z = 0 and a sphere in front of it; scene B has
       no mirror and the sphere moved to its image position behind z = 0.
       The same ray must see the same color in both -- the sun is vertical,
       so the lighting is symmetric under the flip too. */
    HoloScene a = {
        .spheres = { { .center = hv3(1, 1, 2), .radius = 0.5f,
                       .albedo = hv3(0.8f, 0.2f, 0.4f) } },
        .sphere_count = 1,
        .rects = { { .corner = hv3(-4, 0, 0), .edge_u = hv3(8, 0, 0),
                     .edge_v = hv3(0, 4, 0), .albedo = hv3(1, 1, 1),
                     .mirror = 1.0f } },
        .rect_count = 1,
        .sun_dir = hv3(0, 1, 0),
        .horizon = hv3(0.7f, 0.7f, 0.7f), .zenith = hv3(0.1f, 0.1f, 0.1f),
    };
    HoloScene b = a;
    b.rect_count = 0;
    b.spheres[0].center = hv3(1, 1, -2);

    /* Aimed through the mirror at the image point. */
    HoloRay r = { .origin = hv3(2, 1, 4),
                  .dir = hv3_norm(hv3(-1, 0, -6)) };
    HoloV3 through = holo_trace_ray(&a, r);
    HoloV3 unfolded = holo_trace_ray(&b, r);
    check_close(through.x, unfolded.x, "mirror image r");
    check_close(through.y, unfolded.y, "mirror image g");
    check_close(through.z, unfolded.z, "mirror image b");
}

static void test_corridor(void) {
    printf("trace: facing mirrors attenuate per bounce\n");
    /* Two facing mirror panels tinted (0.5, 1, 1): every bounce halves the
       red and leaves green and blue alone. A ray at 45 degrees walks the
       corridor in 4m steps, bounces exactly 5 times before the panels run
       out, and escapes to a sky whose value is known -- so the answer is
       0.5^5 of the red and all of the rest. */
    HoloScene s = {
        .rects = {
            { .corner = hv3(0, 0, -14), .edge_u = hv3(0, 0, 22),
              .edge_v = hv3(0, 2, 0), .albedo = hv3(0.5f, 1, 1), .mirror = 1 },
            { .corner = hv3(4, 0, -14), .edge_u = hv3(0, 0, 22),
              .edge_v = hv3(0, 2, 0), .albedo = hv3(0.5f, 1, 1), .mirror = 1 },
        },
        .rect_count = 2,
        .sun_dir = hv3(0, 1, 0),
        .horizon = hv3(1, 0.8f, 0.6f), .zenith = hv3(0, 0.4f, 0.2f),
    };
    /* dir has y = 0, so the escape sky is the exact horizon/zenith mean. */
    float inv_sqrt2 = 0.70710678f;
    HoloRay r = { .origin = hv3(0, 1, 7),
                  .dir = hv3(inv_sqrt2, 0, -inv_sqrt2) };
    HoloV3 c = holo_trace_ray(&s, r);
    check_close(c.x, 0.03125f * 0.5f, "red halved five times");
    check_close(c.y, 0.6f, "green untouched");
    check_close(c.z, 0.4f, "blue untouched");
}

static void test_depth_cap(void) {
    printf("trace: trapped light gives up dark\n");
    /* Perpendicular between two perfect mirrors the ray never escapes; at
       the bounce cap the walk stops having banked nothing, and the answer
       is black -- not a hang, not a stack, just spent light. */
    HoloScene s = {
        .rects = {
            { .corner = hv3(0, 0, -14), .edge_u = hv3(0, 0, 22),
              .edge_v = hv3(0, 2, 0), .albedo = hv3(1, 1, 1), .mirror = 1 },
            { .corner = hv3(4, 0, -14), .edge_u = hv3(0, 0, 22),
              .edge_v = hv3(0, 2, 0), .albedo = hv3(1, 1, 1), .mirror = 1 },
        },
        .rect_count = 2,
        .sun_dir = hv3(0, 1, 0),
        .horizon = hv3(1, 1, 1), .zenith = hv3(1, 1, 1),
    };
    HoloRay r = { .origin = hv3(2, 1, 0), .dir = hv3(1, 0, 0) };
    HoloV3 c = holo_trace_ray(&s, r);
    check_close(c.x, 0.0f, "no light comes back");
}

static void test_polished_floor(void) {
    printf("trace: a part-mirror splits matte and reflection\n");
    /* Straight down at a half-mirror white floor under a noon sun: the
       matte half banks 0.5, the mirrored half carries 0.5 up to the zenith.
       0.5 + 0.5 * 0.2 = 0.6, by hand. */
    HoloScene s = {
        .has_floor = 1,
        .floor_a = hv3(1, 1, 1), .floor_b = hv3(1, 1, 1),
        .floor_mirror = 0.5f,
        .sun_dir = hv3(0, 1, 0),
        .horizon = hv3(1, 1, 1), .zenith = hv3(0.2f, 0.2f, 0.2f),
    };
    HoloRay r = { .origin = hv3(0.5f, 2, 0.5f), .dir = hv3(0, -1, 0) };
    HoloV3 c = holo_trace_ray(&s, r);
    check_close(c.x, 0.6f, "matte share plus mirrored zenith");
}

static void test_glass_pane(void) {
    printf("trace: a window splits by Fresnel\n");
    /* Straight down through a horizontal pane at a red floor: 4% of the
       light reflects to the zenith, 96% passes to the floor, and the floor
       is NOT in the pane's shadow -- clear glass throws none. Every number
       in the expected color is Fresnel at normal incidence. */
    HoloScene s = {
        .rects = { { .corner = hv3(-5, 2, -5), .edge_u = hv3(10, 0, 0),
                     .edge_v = hv3(0, 0, 10), .albedo = hv3(1, 1, 1),
                     .transmit = 1.0f, .ior = 1.5f } },
        .rect_count = 1,
        .has_floor = 1,
        .floor_a = hv3(1, 0, 0), .floor_b = hv3(1, 0, 0),
        .sun_dir = hv3(0, 1, 0),
        .horizon = hv3(1, 1, 1), .zenith = hv3(0.2f, 0.4f, 0.6f),
    };
    HoloRay r = { .origin = hv3(0.5f, 5, 0.5f), .dir = hv3(0, -1, 0) };
    HoloV3 c = holo_trace_ray(&s, r);
    check_close(c.x, 0.96f + 0.04f * 0.2f, "96% floor + 4% zenith, red");
    check_close(c.y, 0.04f * 0.4f, "green is all reflection");
    check_close(c.z, 0.04f * 0.6f, "blue is all reflection");
}

static void test_glass_sphere_energy(void) {
    printf("trace: glass neither makes nor eats light\n");
    /* A clear glass ball under a uniform white sky, shot through the
       center: every branch ends in sky = 1, so the answer is the sum of
       the branch weights -- 0.04 off the front, 0.96^2 straight through,
       0.96 * 0.04 * 0.96 out the front after one internal bounce, and one
       branch (0.15%) legitimately culled by the throughput floor:
       0.998464 by hand. Energy is conserved to the cull, or the Fresnel
       arithmetic is wrong somewhere. */
    HoloScene s = {
        .spheres = { { .center = hv3(0, 0, 0), .radius = 1,
                       .albedo = hv3(1, 1, 1), .transmit = 1.0f,
                       .ior = 1.5f } },
        .sphere_count = 1,
        .sun_dir = hv3(0, 1, 0),
        .horizon = hv3(1, 1, 1), .zenith = hv3(1, 1, 1),
    };
    HoloRay r = { .origin = hv3(0, 0, 5), .dir = hv3(0, 0, -1) };
    HoloV3 c = holo_trace_ray(&s, r);
    check_close(c.x, 0.998464f, "the branch weights sum");
}

static void test_spectral_agrees_on_gray(void) {
    printf("trace: spectral and RGB agree wherever nothing disperses\n");
    /* A neutral scene traced spectrally must land on the RGB answer
       exactly: gray albedos read the same at every wavelength, achromatic
       glass bends every wavelength alike, and the weights sum to one. Any
       daylight between the two pipelines here is a bug, not physics. */
    HoloScene s = {
        .spheres = { { .center = hv3(0, 1, 0), .radius = 1,
                       .albedo = hv3(0.7f, 0.7f, 0.7f), .transmit = 1.0f,
                       .ior = 1.5f } },
        .sphere_count = 1,
        .has_floor = 1,
        .floor_a = hv3(0.8f, 0.8f, 0.8f), .floor_b = hv3(0.3f, 0.3f, 0.3f),
        .sun_dir = hv3(0, 1, 0),
        .horizon = hv3(0.9f, 0.9f, 0.9f), .zenith = hv3(0.2f, 0.2f, 0.2f),
    };
    HoloRay r = { .origin = hv3(0.4f, 1.6f, 5),
                  .dir = hv3_norm(hv3(-0.1f, -0.2f, -1)) };
    HoloV3 rgb = holo_trace_ray(&s, r);
    HoloV3 spec = holo_trace_ray_spectral(&s, r);
    check_close(spec.x, rgb.x, "spectral r matches");
    check_close(spec.y, rgb.y, "spectral g matches");
    check_close(spec.z, rgb.z, "spectral b matches");
}

static void test_dispersion_fringes(void) {
    printf("trace: dispersive glass tears white light apart\n");
    /* Through a strongly dispersive pane... no -- panes do not bend, so
       dispersion must come from a volume. A ray through a flint ball
       off-center: the RGB trace calls the result neutral (gray world,
       single ior), but the spectral trace bends blue and red onto
       different exit paths toward different-brightness sky, so the pixel
       must come out colored. Fringing is not an artifact: it is the test. */
    HoloScene s = {
        .spheres = { { .center = hv3(0, 0, 0), .radius = 1,
                       .albedo = hv3(1, 1, 1), .transmit = 1.0f,
                       .ior = 1.62f, .disperse = 0.03f } },
        .sphere_count = 1,
        .sun_dir = hv3(0, 1, 0),
        /* A sky that changes fast with direction, so a small angular split
           becomes a big intensity split. */
        .horizon = hv3(1, 1, 1), .zenith = hv3(0, 0, 0),
    };
    HoloRay r = { .origin = hv3(0.55f, 3, 0.0f), .dir = hv3(0, -1, 0) };
    HoloV3 c = holo_trace_ray_spectral(&s, r);
    float spread = fabsf(c.x - c.z);
    check(spread > 0.01f, "red and blue no longer agree");
}

int main(void) {
    test_camera();
    test_shading();
    test_image();
    test_mirror_image();
    test_corridor();
    test_depth_cap();
    test_polished_floor();
    test_glass_pane();
    test_glass_sphere_energy();
    test_spectral_agrees_on_gray();
    test_dispersion_fringes();
    return report();
}
