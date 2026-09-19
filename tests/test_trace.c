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

/* A polarizer pane facing the camera, axis at deg degrees. */
static HoloRect pane(float z, float deg) {
    return (HoloRect){
        .corner = hv3(-3, -3, z), .edge_u = hv3(6, 0, 0),
        .edge_v = hv3(0, 6, 0), .albedo = hv3(1, 1, 1),
        .filter = HOLO_POLARIZER,
        .filter_angle = deg * 3.14159265f / 180.0f,
    };
}

static void test_polarizers_in_scene(void) {
    printf("trace: the three-polarizer paradox, in a scene\n");
    /* A white sky behind stacked polarizer panes. Two crossed: dark.
       Slide a 45-degree one between them: an eighth of the sky returns.
       This is the whole Mueller machine running inside the actual walk. */
    HoloScene s = {
        .rects = { pane(1.0f, 0), pane(0.0f, 90) },
        .rect_count = 2,
        .sun_dir = hv3(0, 1, 0),
        .horizon = hv3(1, 1, 1), .zenith = hv3(1, 1, 1),
    };
    HoloRay r = { .origin = hv3(0.3f, 0.2f, 5), .dir = hv3(0, 0, -1) };
    float crossed = holo_trace_lambda(&s, r, 0.55f);
    check_close(crossed, 0.0f, "crossed panes: dark sky");

    s.rects[2] = pane(0.5f, 45);
    s.rect_count = 3;
    float three = holo_trace_lambda(&s, r, 0.55f);
    check_close(three, 0.125f, "a third pane brings back an eighth");
}

static void test_waveplate_colors(void) {
    printf("trace: a waveplate between crossed polarizers writes color\n");
    /* A full-wave plate (at the D line) at 45 degrees between crossed
       polarizers: at 589nm it does nothing, so the crossed pair stays
       dark -- but its retardance runs as 1/lambda, so blue light sees
       more than a full wave and leaks through. One element, dark at one
       wavelength and bright at another: interference color, from physics
       the RGB pipeline cannot even express. */
    HoloRect wp = pane(0.5f, 45);
    wp.filter = HOLO_WAVEPLATE;
    wp.retard = 2.0f * 3.14159265f;
    HoloScene s = {
        .rects = { pane(1.0f, 0), pane(0.0f, 90), wp },
        .rect_count = 3,
        .sun_dir = hv3(0, 1, 0),
        .horizon = hv3(1, 1, 1), .zenith = hv3(1, 1, 1),
    };
    HoloRay r = { .origin = hv3(0.3f, 0.2f, 5), .dir = hv3(0, 0, -1) };
    float at_d = holo_trace_lambda(&s, r, 0.5893f);
    float at_blue = holo_trace_lambda(&s, r, 0.44f);
    check_close(at_d, 0.0f, "the D line stays extinguished");
    check(at_blue > 0.2f, "blue leaks through the same element");
}

static void test_solar_furnace(void) {
    printf("trace: at the focus, the whole dish is the sun\n");
    /* A paraboloid aimed at the sun, the eye at its focus: EVERY ray from
       the focus to the dish reflects exactly into the sun disk, so every
       point of the dish shows sun intensity -- the reason solar furnace
       aperture views are blinding, reproduced by backward tracing alone.
       Off the focus, the same rays miss the disk and see plain sky. */
    HoloScene s = {
        .dishes = { { .apex = hv3(0, 0, 0), .axis = hv3(0, 0, 1),
                      .curv_r = 2.0f, .conic_k = -1.0f, .rim = 1.5f,
                      .albedo = hv3(1, 1, 1), .mirror = 1.0f } },
        .dish_count = 1,
        .sun_dir = hv3(0, 0, 1),
        .sun_disk_cos = 0.9994f,   /* about a 2-degree disk */
        .sun_disk_intensity = 25.0f,
        .horizon = hv3(0.4f, 0.4f, 0.4f), .zenith = hv3(0.4f, 0.4f, 0.4f),
    };
    /* From the focus (0,0,1) toward an off-axis point of the dish. */
    HoloV3 target = hv3(1.0f, 0, 0.25f);   /* the r=1 zone, z = r^2/2R */
    HoloRay r = { .origin = hv3(0, 0, 1),
                  .dir = hv3_norm(hv3_sub(target, hv3(0, 0, 1))) };
    float lit = holo_trace_lambda(&s, r, 0.55f);
    check_close(lit, 25.0f, "the zone shows full sun intensity");

    /* The same aim from half a meter off the focus: no sun. */
    r.origin = hv3(0, 0.5f, 1);
    r.dir = hv3_norm(hv3_sub(target, r.origin));
    float unlit = holo_trace_lambda(&s, r, 0.55f);
    check_close(unlit, 0.4f, "off the focus, plain sky in the mirror");
}

static void test_dish_shadow(void) {
    printf("trace: a dish shades the ground under it\n");
    /* A shallow paraboloid three meters up, opening at the noon sun, over
       a plain grey floor. A dish is never glass, so unlike a window it
       throws an ordinary hard shadow; the eye looking straight down passes
       under the rim without touching it. */
    HoloScene s = {
        .dishes = { { .apex = hv3(0, 3, 0), .axis = hv3(0, 1, 0),
                      .curv_r = 20.0f, .conic_k = -1.0f, .rim = 2.0f,
                      .albedo = hv3(1, 1, 1), .mirror = 1.0f } },
        .dish_count = 1,
        .has_floor = 1,
        .floor_a = hv3(0.9f, 0.9f, 0.9f),
        .floor_b = hv3(0.9f, 0.9f, 0.9f),
        .sun_dir = hv3(0, 1, 0),
        .horizon = hv3(1, 1, 1), .zenith = hv3(0.2f, 0.4f, 0.6f),
    };

    /* Half a meter off the axis: well inside the rim, so the shadow ray
       up from the floor meets the bowl. */
    HoloRay under = { .origin = hv3(0.5f, 1, 0), .dir = hv3(0, -1, 0) };
    HoloV3 c = holo_trace_ray(&s, under);
    check_close(c.x, 0.9f * HOLO_AMBIENT, "floor under the dish is ambient");

    /* Five meters out, past a rim of two: the sun gets through. Note the
       shadow ray does still cross the paraboloid's SURFACE out here -- it
       is the rim clip, not a miss, that lets the light by. */
    HoloRay beside = { .origin = hv3(5, 1, 0), .dir = hv3(0, -1, 0) };
    c = holo_trace_ray(&s, beside);
    check_close(c.x, 0.9f, "floor past the rim is fully lit");

    /* And on the spectral path, which shades in a separate statement. */
    float dark = holo_trace_lambda(&s, under, 0.55f);
    float lit = holo_trace_lambda(&s, beside, 0.55f);
    check(dark < 0.25f * lit, "the spectral path shades it too");
}

static void test_grating_orders(void) {
    printf("trace: a grating fans light into its orders\n");
    /* Normal incidence on a 1um grating under a uniform white sky, every
       order weighted 0.2. At 550nm the first orders leave at +-33 degrees
       and the second would need sin = 1.1 -- evanescent -- so exactly
       three branches reach the sky: 0.6, by hand. At 450nm the second
       order squeaks out at sin = 0.9 and the answer rises to 0.8. An
       order appearing as the wavelength shortens is the grating equation
       audited by addition. */
    HoloScene s = {
        .rects = { { .corner = hv3(-3, -3, 0), .edge_u = hv3(6, 0, 0),
                     .edge_v = hv3(0, 6, 0), .albedo = hv3(1, 1, 1),
                     .grating_period = 1.0f,
                     .order_w = { 0.2f, 0.2f, 0.2f, 0.2f } } },
        .rect_count = 1,
        .sun_dir = hv3(0, 1, 0),
        .horizon = hv3(1, 1, 1), .zenith = hv3(1, 1, 1),
    };
    HoloRay r = { .origin = hv3(0.3f, 0.2f, 5), .dir = hv3(0, 0, -1) };
    check_close(holo_trace_lambda(&s, r, 0.55f), 0.6f,
                "three orders propagate at 550nm");
    check_close(holo_trace_lambda(&s, r, 0.45f), 0.8f,
                "the second order joins at 450nm");
}

/* A biconvex lens of two dishes, centred on the origin, optical axis +z.
 *
 * A dish is the surface z = r^2 / 2R in its own frame, a bowl opening along
 * its axis. So a lens is two of them back to back:
 *
 *   front  apex at -t/2, axis +z  -- bulges toward -z, the incoming side
 *   rear   apex at +t/2, axis -z  -- bulges toward +z
 *
 * Light going +z enters the front, travels inside the glass, and leaves the
 * rear converging. Backward from a point behind it -- which is how this tracer
 * works -- a ray from the focus refracts at both surfaces and emerges parallel
 * to the axis, into the sun. That is the same argument the paraboloid focus
 * test makes, with Snell twice instead of a reflection once. */
static void build_lens(HoloScene *s, float thick, float curv_r, float rim,
                       float ior_d, float cauchy_b) {
    s->dishes[0] = (HoloDish){
        .apex = hv3(0, 0, -thick * 0.5f), .axis = hv3(0, 0, 1),
        .curv_r = curv_r, .conic_k = 0.0f, .rim = rim,
        .albedo = hv3(1, 1, 1), .mirror = 0.0f,
        .transmit = 1.0f, .ior = ior_d, .disperse = cauchy_b,
    };
    s->dishes[1] = (HoloDish){
        .apex = hv3(0, 0, thick * 0.5f), .axis = hv3(0, 0, -1),
        .curv_r = curv_r, .conic_k = 0.0f, .rim = rim,
        .albedo = hv3(1, 1, 1), .mirror = 0.0f,
        .transmit = 1.0f, .ior = ior_d, .disperse = cauchy_b,
    };
    s->dish_count = 2;
}

/* Where the lens brings one wavelength to a point, found by looking. Trace
   from a series of positions along the axis toward a fixed off-axis zone of
   the lens, and take the position that sees the sun brightest. The tracer is
   the instrument; nothing here assumes a focal length. */
static float measure_focus(HoloScene *s, float lambda_um, float zone_r,
                           float lo, float hi, float step, float *peak_out) {
    float best_z = lo, best = -1.0f;
    for (float z = lo; z <= hi; z += step) {
        HoloV3 eye = hv3(0, 0, z);
        HoloV3 target = hv3(zone_r, 0, 0);
        HoloRay r = { .origin = eye, .dir = hv3_norm(hv3_sub(target, eye)) };
        float v = holo_trace_lambda(s, r, lambda_um);
        if (v > best) { best = v; best_z = z; }
    }
    if (peak_out) *peak_out = best;
    return best_z;
}

static void test_lens_focuses(void) {
    printf("trace: a lens brings the sun to a focus\n");
    /* A ray from the focus, out through the lens, must land in the sun disk;
       a ray from well off the focus must not. This is the refracting twin of
       the paraboloid test above, and it is the whole reason a dish was given
       glass. */
    HoloScene s = {
        .sun_dir = hv3(0, 0, -1),
        .sun_disk_cos = 0.9994f,   /* about a 2-degree disk */
        .sun_disk_intensity = 25.0f,
        .horizon = hv3(0.3f, 0.3f, 0.3f), .zenith = hv3(0.3f, 0.3f, 0.3f),
    };
    build_lens(&s, 0.30f, 2.0f, 1.0f, 1.5168f, 0.0f); /* achromatic BK7 */

    float peak = 0.0f;
    float f = measure_focus(&s, 0.55f, 0.7f, 0.5f, 4.0f, 0.01f, &peak);
    printf("  focus at z = %.2f m, peak %.1f\n", (double)f, (double)peak);

    check(peak > 15.0f, "at the focus the lens fills with sun");
    /* The thin-lens prediction for a symmetric biconvex, f = R / 2(n-1), is
       1.94 m. This is a THICK lens, so agreement is expected to be loose
       rather than exact -- the claim is that the tracer focuses near where
       optics says it should, not that it reproduces a formula it does not
       use. */
    check(f > 1.4f && f < 2.6f, "and near where thin-lens optics predicts");

    /* Well inside the focus, the same aim sees plain sky through the glass. */
    HoloV3 target = hv3(0.7f, 0, 0);
    HoloRay r = { .origin = hv3(0, 0, 0.6f) };
    r.dir = hv3_norm(hv3_sub(target, r.origin));
    float off = holo_trace_lambda(&s, r, 0.55f);
    check(off < 5.0f, "off the focus, no sun through the lens");
}

static void test_lens_has_chromatic_aberration(void) {
    printf("trace: blue comes to a focus before red\n");
    /* THE CLAIM ONLY A SPECTRAL TRACER CAN MAKE. Glass is more strongly
       refracting at short wavelengths, so a single lens has no one focal
       length: blue focuses nearer the glass than red, which is why every
       refracting telescope before the achromat had a purple fringe and why
       Newton built a reflector instead.
     *
       Measured rather than predicted. Both wavelengths go through the same
       tracer with the same geometry and the only difference is n(lambda), so
       what is asserted is the ORDER -- which no thin-lens formula is needed to
       trust, and which comes out wrong if dispersion is dropped anywhere in
       the four tracers. */
    HoloScene s = {
        .sun_dir = hv3(0, 0, -1),
        .sun_disk_cos = 0.99995f,  /* a sharp disk, so the focus is sharp */
        .sun_disk_intensity = 25.0f,
        .horizon = hv3(0.3f, 0.3f, 0.3f), .zenith = hv3(0.3f, 0.3f, 0.3f),
    };
    /* Dense flint, several times BK7's dispersion, so the two foci are far
       enough apart to separate at the step this scan can afford. */
    build_lens(&s, 0.30f, 2.0f, 1.0f, 1.62f, 0.025f);

    float pb = 0.0f, pr = 0.0f;
    float f_blue = measure_focus(&s, 0.45f, 0.7f, 0.8f, 3.0f, 0.005f, &pb);
    float f_red = measure_focus(&s, 0.65f, 0.7f, 0.8f, 3.0f, 0.005f, &pr);
    printf("  blue focus %.3f m (peak %.1f), red %.3f m (peak %.1f), "
           "spread %.0f mm\n",
           (double)f_blue, (double)pb, (double)f_red, (double)pr,
           (double)((f_red - f_blue) * 1000.0f));

    check(pb > 15.0f && pr > 15.0f, "both wavelengths focus at all");
    check(f_blue < f_red, "blue focuses nearer the lens than red");
    check(f_red - f_blue > 0.01f, "and the two are measurably apart");
}

static void test_a_lens_is_not_a_stone(void) {
    printf("trace: a lens lights the ground it stands over\n");
    /* The dish shadow test's opposite number. A mirror dish throws a hard
       shadow; a lens is glass and lets the sun through, so the floor beneath
       it is lit. Without the transmit test in sun_blocked a lens would shade
       the ground like a rock, which is the most visible thing a lens can get
       wrong and is exactly what the mirror-dish shadow code did before. */
    HoloScene s = {
        .has_floor = 1,
        .floor_a = hv3(0.9f, 0.9f, 0.9f),
        .floor_b = hv3(0.9f, 0.9f, 0.9f),
        .sun_dir = hv3(0, 1, 0),
        .horizon = hv3(1, 1, 1), .zenith = hv3(0.2f, 0.4f, 0.6f),
    };
    /* The same lens, lying flat three metres up with the sun overhead. */
    s.dishes[0] = (HoloDish){ .apex = hv3(0, 2.85f, 0), .axis = hv3(0, 1, 0),
                              .curv_r = 20.0f, .conic_k = 0.0f, .rim = 2.0f,
                              .albedo = hv3(1, 1, 1), .mirror = 0.0f,
                              .transmit = 1.0f, .ior = 1.5168f,
                              .disperse = 0.0f };
    s.dishes[1] = (HoloDish){ .apex = hv3(0, 3.15f, 0), .axis = hv3(0, -1, 0),
                              .curv_r = 20.0f, .conic_k = 0.0f, .rim = 2.0f,
                              .albedo = hv3(1, 1, 1), .mirror = 0.0f,
                              .transmit = 1.0f, .ior = 1.5168f,
                              .disperse = 0.0f };
    s.dish_count = 2;

    /* Looking down at the floor from beside the lens, at a point under it. */
    HoloRay r = { .origin = hv3(0, 1.0f, 0), .dir = hv3(0, -1, 0) };
    float lit = holo_trace_lambda(&s, r, 0.55f);

    /* The same scene with the lens made a mirror instead. */
    s.dishes[0].transmit = 0.0f; s.dishes[0].mirror = 1.0f;
    s.dishes[1].transmit = 0.0f; s.dishes[1].mirror = 1.0f;
    float shaded = holo_trace_lambda(&s, r, 0.55f);

    printf("  under glass %.3f, under mirror %.3f\n",
           (double)lit, (double)shaded);
    check(lit > shaded * 1.5f, "glass lets the sun through and a mirror does not");
}

/* The Fresnel lens the tests below share, and the one geometry's tests use:
   f = 0.7 m in n = 1.5 glass, twenty 30 mm rings out to 0.6 m in a 50 mm
   slab, grooves toward +z and the focus at z = 0.7. */
static HoloFresnel fresnel_lens(float ior_d, float cauchy_b) {
    return (HoloFresnel){
        .center = hv3(0, 0, 0), .axis = hv3(0, 0, 1),
        .focal = 0.7f, .r0 = 0.0f, .pitch = 0.03f, .rim = 0.6f, .thick = 0.05f,
        .albedo = hv3(1, 1, 1), .mirror = 0.0f,
        .transmit = 1.0f, .ior = ior_d, .disperse = cauchy_b,
    };
}

static void test_fresnel_collimates(void) {
    printf("trace: from the focus, a Fresnel lens fills with sun -- exactly\n");
    /* The lighthouse, run backwards from the lamp. A ray from the focus
       through ring k refracts to run down the axis inside the slab, leaves
       the flat back at normal incidence still parallel, and lands in a sun
       disk a quarter of a degree wide. Unlike the dish lens, whose focal
       length is emergent and is only bracketed, a Fresnel's rings were CUT
       for this focus, so the reading is predicted to the last digit:
       sun times the two Fresnel transmittances, the facet's at the angle
       the geometry says the ray arrives at, the back's at normal incidence.

       The sky is black on purpose. Every other branch -- the facet's
       reflection, the slab's internal bounces -- ends somewhere that is not
       the sun, so with no sky to bank it contributes nothing and the sum is
       one term. That is what makes this exact rather than approximate. */
    HoloScene s = {
        .fresnels = { fresnel_lens(1.5f, 0.0f) },
        .fresnel_count = 1,
        .sun_dir = hv3(0, 0, -1),
        .sun_disk_cos = 0.99999f,   /* a quarter of a degree */
        .sun_disk_intensity = 25.0f,
        .horizon = hv3(0, 0, 0), .zenith = hv3(0, 0, 0),
    };
    const HoloFresnel *f = &s.fresnels[0];
    HoloV3 focus = hv3(0, 0, f->focal);
    int ks[] = { 3, 5, 12 };
    for (int i = 0; i < 3; i++) {
        /* Just outside ring k's peak, where the tilt is exact for r_k and
           the corner with the previous riser is not in question. */
        float rk = (float)ks[i] * f->pitch, off = 0.05f * f->pitch;
        float a = holo_fresnel_tilt(rk, f->focal, f->ior);
        HoloV3 target = hv3(rk + off, 0, -off * tanf(a));
        HoloRay r = { .origin = focus, .dir = hv3_norm(hv3_sub(target, focus)) };

        /* What the geometry says about the arrival, independently of the
           walk: the incidence angle at the facet fixes T there. */
        HoloHit h;
        check_int(holo_ray_fresnel(r, f->center, f->axis, f->focal, f->ior,
                                   f->r0, f->pitch, f->rim, f->thick, &h),
                  1, "the ray from the focus meets the facet");
        float rs, rp;
        holo_fresnel(-hv3_dot(h.normal, r.dir), 1.0f, f->ior, &rs, &rp);
        float t_facet = 1.0f - 0.5f * (rs + rp);
        float t_back = 1.0f - 0.04f;   /* n = 1.5 at normal incidence */
        float want = 25.0f * t_facet * t_back;

        check_close(holo_trace_lambda(&s, r, 0.55f), want,
                    "the spectral walk reads sun times two transmittances");
        check_close(holo_trace_ray(&s, r).x, want,
                    "and so does the RGB walk");
    }

    /* Half a metre off the focus the same aim leaves the slab askew and
       misses the disk: black sky through the glass. */
    HoloV3 target = hv3(5 * f->pitch + 0.0015f, 0, -0.001f);
    HoloRay r = { .origin = hv3(0, 0.5f, f->focal) };
    r.dir = hv3_norm(hv3_sub(target, r.origin));
    check_close(holo_trace_lambda(&s, r, 0.55f), 0.0f,
                "off the focus, no sun through the lens");
}

static void test_fresnel_has_chromatic_aberration(void) {
    printf("trace: a Fresnel cut for the D line misses at blue\n");
    /* The rings are tilted for n at the D line. Trace flint at 450 nm and
       n is 0.05 higher: the ray inside the slab leans 0.6 degrees off the
       axis, and the flat back face makes that worse rather than better --
       an internal lean of e leaves at n e, since Snell runs the other way
       on the way out -- so it emerges a full degree off and misses a
       half-degree sun disk. At 650 nm the lean is a quarter of blue's,
       0.3 degrees on exit, and the disk still catches it. This is the
       guard on the design index: compute the tilt with holo_ior_at(lambda)
       instead of ior and every wavelength collimates perfectly, blue reads
       full sun, and this fails. */
    HoloScene s = {
        .fresnels = { fresnel_lens(1.62f, 0.025f) },   /* dense flint */
        .fresnel_count = 1,
        .sun_dir = hv3(0, 0, -1),
        .sun_disk_cos = 0.99995f,   /* half a degree */
        .sun_disk_intensity = 25.0f,
        .horizon = hv3(0, 0, 0), .zenith = hv3(0, 0, 0),
    };
    const HoloFresnel *f = &s.fresnels[0];
    HoloV3 focus = hv3(0, 0, f->focal);
    float rk = 5 * f->pitch, off = 0.02f * f->pitch;
    float a = holo_fresnel_tilt(rk, f->focal, f->ior);
    HoloV3 target = hv3(rk + off, 0, -off * tanf(a));
    HoloRay r = { .origin = focus, .dir = hv3_norm(hv3_sub(target, focus)) };

    float at_d = holo_trace_lambda(&s, r, 0.5893f);
    float at_blue = holo_trace_lambda(&s, r, 0.45f);
    float at_red = holo_trace_lambda(&s, r, 0.65f);
    printf("  D %.2f, red %.2f, blue %.2f\n",
           (double)at_d, (double)at_red, (double)at_blue);
    check(at_d > 15.0f, "the D line, which the rings were cut for, sees the sun");
    check(at_blue < 1.0f, "blue leans off the axis and misses the disk");
    check(at_red > at_blue, "red leans less than blue");
}

static void test_fresnel_spectral_agrees_on_gray(void) {
    printf("trace: through an achromatic Fresnel, spectral and RGB agree "
           "where they can\n");
    /* A neutral scene seen through the lens. Straight in through the flat
       central disc the two walks must land on the same number exactly:
       every interface is at normal incidence, where s and p are one
       coefficient, so nothing separates a Stokes vector from a scalar.

       At a slant they are ALLOWED to differ, and do, by two percent -- and
       that is the physics, not a bug. The oblique ray's first surface is a
       riser met at eighty degrees, which polarizes it hard (rs 0.52 against
       rp 0.22); it then TIRs off the back, climbs out of one groove and
       into the next through a facet at Brewster's angle, and leaves after
       eight interactions where a dish lens has two. The spectral walk
       carries that polarization into every later interface; the RGB walk
       averages s and p at each and cannot know, the same way its polarizer
       is a flat 50%. The bound below is loose on purpose: it is there to
       catch a surface carried through one walk and not the other -- a
       stranded inside flag reads as a factor, not a few percent. */
    HoloScene s = {
        .fresnels = { fresnel_lens(1.5f, 0.0f) },
        .fresnel_count = 1,
        .has_floor = 1,
        .floor_y = -1.0f,
        .floor_a = hv3(0.8f, 0.8f, 0.8f), .floor_b = hv3(0.3f, 0.3f, 0.3f),
        .sun_dir = hv3(0, 1, 0),
        .horizon = hv3(0.9f, 0.9f, 0.9f), .zenith = hv3(0.2f, 0.2f, 0.2f),
    };
    HoloRay axial = { .origin = hv3(0.01f, 0.3f, 1.0f), .dir = hv3(0, 0, -1) };
    HoloV3 rgb = holo_trace_ray(&s, axial);
    HoloV3 spec = holo_trace_ray_spectral(&s, axial);
    check(rgb.x > 0.0f, "the axial ray sees something");
    check_close(spec.x, rgb.x, "at normal incidence, spectral r matches");
    check_close(spec.y, rgb.y, "spectral g matches");
    check_close(spec.z, rgb.z, "spectral b matches");

    HoloRay slant = { .origin = hv3(0.2f, 0.3f, 1.0f),
                      .dir = hv3_norm(hv3(0.05f, -0.4f, -1)) };
    rgb = holo_trace_ray(&s, slant);
    spec = holo_trace_ray_spectral(&s, slant);
    printf("  at a slant: rgb %.4f, spectral %.4f\n",
           (double)rgb.x, (double)spec.x);
    check(fabsf(spec.x - rgb.x) < 0.05f * rgb.x,
          "at a slant the two agree to polarization, not beyond");
}

static void test_a_fresnel_is_not_a_stone(void) {
    printf("trace: a Fresnel lens lights the ground it stands over\n");
    /* The lens lying flat three metres up, grooves to the noon sun, over a
       grey floor: glass, and the floor beneath is lit; made a mirror, and
       it is in shadow. The sun_blocked clause, stated for the fifth shape. */
    HoloScene s = {
        .fresnels = { fresnel_lens(1.5f, 0.0f) },
        .fresnel_count = 1,
        .has_floor = 1,
        .floor_a = hv3(0.9f, 0.9f, 0.9f),
        .floor_b = hv3(0.9f, 0.9f, 0.9f),
        .sun_dir = hv3(0, 1, 0),
        .horizon = hv3(1, 1, 1), .zenith = hv3(0.2f, 0.4f, 0.6f),
    };
    s.fresnels[0].center = hv3(0, 3, 0);
    s.fresnels[0].axis = hv3(0, 1, 0);

    HoloRay r = { .origin = hv3(0.2f, 1.0f, 0), .dir = hv3(0, -1, 0) };
    float lit = holo_trace_lambda(&s, r, 0.55f);

    s.fresnels[0].transmit = 0.0f;
    s.fresnels[0].mirror = 1.0f;
    float shaded = holo_trace_lambda(&s, r, 0.55f);

    printf("  under glass %.3f, under mirror %.3f\n",
           (double)lit, (double)shaded);
    check(lit > shaded * 1.5f, "glass lets the sun through and a mirror does not");
}

/* One upward-facing matte panel, one facing sideways and one facing down,
   over nothing: three normals for the lighting laws below to be read off. */
static HoloScene three_panels(void) {
    HoloScene s = {
        .rects = {
            { .corner = hv3(-1, 0, -1), .edge_u = hv3(2, 0, 0),
              .edge_v = hv3(0, 0, 2), .albedo = hv3(0.8f, 0.6f, 0.4f) },
            { .corner = hv3(10, -1, -1), .edge_u = hv3(0, 2, 0),
              .edge_v = hv3(0, 0, 2), .albedo = hv3(0.8f, 0.6f, 0.4f) },
        },
        .rect_count = 2,
        .sun_dir = hv3(0, 1, 0),
    };
    return s;
}

static const HoloRay ONTO_THE_TOP  = { { 0, 5, 0 },  { 0, -1, 0 } };
static const HoloRay ONTO_THE_BACK = { { 0, -5, 0 }, { 0, 1, 0 } };
/* Half a metre up, so as not to run along the first panel's own plane. */
static const HoloRay ONTO_THE_SIDE = { { 5, 0.5f, 0 }, { 1, 0, 0 } };

static void test_sun_color(void) {
    printf("trace: the sun has a colour, and none is white\n");
    HoloScene s = three_panels();

    check_close(holo_sun_color(&s).x, 1.0f, "an unset sun colour reads white");
    check_close(holo_sun_color(&s).z, 1.0f, "in every channel");

    /* Unset and explicitly white are the same scene, to the last bit: the
       promise that no scene written before this field changed. */
    HoloV3 unset = holo_trace_ray(&s, ONTO_THE_TOP);
    float unset_l = holo_trace_lambda(&s, ONTO_THE_TOP, 0.55f);
    s.sun_color = hv3(1, 1, 1);
    HoloV3 white = holo_trace_ray(&s, ONTO_THE_TOP);
    check(unset.x == white.x && unset.y == white.y && unset.z == white.z,
          "unset and white trace bit for bit alike, RGB");
    check(unset_l == holo_trace_lambda(&s, ONTO_THE_TOP, 0.55f),
          "and spectrally");
    check_close(white.x, 0.8f, "full white sun still returns exactly the albedo");

    /* A coloured sun multiplies, channel by channel, light and shade alike
       (the ambient stand-in is the sun's own bounce, so it takes the tint). */
    s.sun_color = hv3(1.0f, 0.5f, 0.0f);
    HoloV3 lit = holo_trace_ray(&s, ONTO_THE_TOP);
    check_close(lit.x, 0.8f, "red passes whole");
    check_close(lit.y, 0.3f, "green at half");
    check_close(lit.z, 0.0f, "and no blue arrives to be reflected");
    HoloV3 shade = holo_trace_ray(&s, ONTO_THE_BACK);
    check_close(shade.y, 0.6f * 0.5f * HOLO_AMBIENT, "the shaded face is tinted too");

    /* The safe light. Yellow has no blue, and holo_albedo_at's blue band
       runs out by 0.51 um, so below 0.475 a white panel under it is black
       and above 0.51 it is as bright as under white light. */
    s.rects[0].albedo = hv3(1, 1, 1);
    s.sun_color = hv3(1, 1, 0);
    check_close(holo_trace_lambda(&s, ONTO_THE_TOP, 0.42f), 0.0f,
                "yellow light: nothing at 420 nm");
    check_close(holo_trace_lambda(&s, ONTO_THE_TOP, 0.4673f), 0.0f,
                "nor at 467 nm");
    check_close(holo_trace_lambda(&s, ONTO_THE_TOP, 0.55f), 1.0f,
                "all of it at 550 nm");
    check_close(holo_trace_lambda(&s, ONTO_THE_TOP, 0.65f), 1.0f,
                "and at 650 nm");

    /* The disk is the sun, so it wears the colour. */
    s.sun_disk_cos = 0.99f;
    s.sun_disk_intensity = 20.0f;
    HoloRay up = { .origin = hv3(50, 1, 0), .dir = hv3(0, 1, 0) };
    HoloV3 disk = holo_trace_ray(&s, up);
    check_close(disk.x, 20.0f, "the disk is its intensity in red");
    check_close(disk.z, 0.0f, "and absent in blue");
    check_close(holo_trace_lambda(&s, up, 0.42f), 0.0f,
                "a yellow sun has no 420 nm disk for a grating to throw");
}

static void test_sky_light(void) {
    printf("trace: the sky as a light, by the white furnace\n");
    HoloScene s = three_panels();
    s.rects[0].albedo = hv3(1, 1, 1);
    s.rects[1].albedo = hv3(1, 1, 1);

    /* THE WHITE FURNACE. A white matte surface inside a uniform sky, with no
       sun on it, shows exactly the sky's radiance whichever way it faces:
       it cannot be told from the sky behind it. */
    s.horizon = hv3(0.5f, 0.4f, 0.3f);
    s.zenith = s.horizon;
    s.sky_light = 1.0f;
    HoloV3 back = holo_trace_ray(&s, ONTO_THE_BACK);
    HoloV3 side = holo_trace_ray(&s, ONTO_THE_SIDE);
    check_close(back.x, 0.5f, "furnace: facing down, r");
    check_close(back.y, 0.4f, "g");
    check_close(back.z, 0.3f, "b");
    check_close(side.x, 0.5f, "furnace: facing sideways, the same");
    check_close(side.z, 0.3f, "in every channel");

    /* The sun adds to the sky; it does not share a budget with it. */
    HoloV3 top = holo_trace_ray(&s, ONTO_THE_TOP);
    check_close(top.x, 1.0f + 0.5f, "sunlit and skylit: the two add");
    s.sun_color = hv3(1, 1, 0);
    top = holo_trace_ray(&s, ONTO_THE_TOP);
    check_close(top.z, 0.3f, "under a yellow sun the blue is the sky's alone");
    s.sun_color = hv3(0, 0, 0);

    /* A graded sky: L = a + b y, E = pi a + (2 pi / 3) b n.y, so a Lambert
       surface shows a + (2/3) b n.y. Horizon 0.2 and zenith 0.8 make
       a = 0.5 and b = 0.3: 0.7 facing up, 0.5 on edge, 0.3 facing down. */
    s.horizon = hv3(0.2f, 0.2f, 0.2f);
    s.zenith = hv3(0.8f, 0.8f, 0.8f);
    s.sun_dir = hv3(0, -1, 0);      /* the sun below, lighting the back */
    check_close(holo_trace_ray(&s, ONTO_THE_TOP).x, 0.7f, "graded sky, facing up");
    check_close(holo_trace_ray(&s, ONTO_THE_SIDE).x, 0.5f, "on edge");
    s.sun_dir = hv3(0, 1, 0);
    check_close(holo_trace_ray(&s, ONTO_THE_BACK).x, 0.3f, "facing down");

    /* It scales, it takes the albedo, and the spectral walk agrees. */
    s.sky_light = 0.5f;
    check_close(holo_trace_ray(&s, ONTO_THE_SIDE).x, 0.25f, "half the sky light");
    s.rects[1].albedo = hv3(0.4f, 0.4f, 0.4f);
    check_close(holo_trace_ray(&s, ONTO_THE_SIDE).x, 0.10f, "times the albedo");
    check_close(holo_trace_lambda(&s, ONTO_THE_SIDE, 0.55f), 0.10f,
                "and the same at 550 nm");

    /* Off, it is the old stand-in exactly. */
    s.sky_light = 0.0f;
    check_close(holo_trace_ray(&s, ONTO_THE_SIDE).x, 0.4f * HOLO_AMBIENT,
                "sky_light 0 is HOLO_AMBIENT, as it always was");
}

int main(void) {
    test_camera();
    test_shading();
    test_sun_color();
    test_sky_light();
    test_image();
    test_mirror_image();
    test_corridor();
    test_depth_cap();
    test_polished_floor();
    test_glass_pane();
    test_glass_sphere_energy();
    test_spectral_agrees_on_gray();
    test_dispersion_fringes();
    test_polarizers_in_scene();
    test_waveplate_colors();
    test_solar_furnace();
    test_dish_shadow();
    test_grating_orders();
    test_lens_focuses();
    test_lens_has_chromatic_aberration();
    test_a_lens_is_not_a_stone();
    test_fresnel_collimates();
    test_fresnel_has_chromatic_aberration();
    test_fresnel_spectral_agrees_on_gray();
    test_a_fresnel_is_not_a_stone();
    return report();
}
