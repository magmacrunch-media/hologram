/* M9: diffraction gratings -- the engine learns the instrument its author
 * did a PhD on.
 *
 * Two ruled panels stand in the field under a fat low sun. Each eye-ray
 * that lands on one fans into its propagating orders by the conical
 * grating equation; the orders that find the sun disk paint the panel.
 * The zeroth order is a plain mirror glint, the first orders are the
 * sun's spectra thrown left and right -- and they sweep across the panel
 * as you walk, the way a CD tilts its colors, because the geometry the
 * colors live on is your own eye position. The finer panel disperses
 * harder: same sun, wider rainbow.
 *
 *   build\m9_spectrum.exe          WASD walk, mouse look, T toggles
 *                                  spectral (and the colors vanish --
 *                                  RGB light has no wavelength to sort)
 *   build\m9_spectrum.exe --diff   hold the still frame to the CPU oracle
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../../external/sokol/sokol_app.h"
#include "../../hologram.h"

#define PI 3.14159265f

static HoloGpuScene gpu;
static HoloScene scene;
static HoloWalkWorld world;
static HoloWalker walker;
static HoloInput input;
static char shader_src[65536];
static int diff_mode;
static int frames_drawn;
static int spectral_on = 1;
static int t_was_down;
static float yaw = PI;
static float pitch;

#define EYE 1.55f
#define WALK_SPEED 3.5f
#define JUMP 6.0f

static HoloCamera camera(void) {
    HoloV3 eye = hv3(walker.pos.x, walker.pos.y + EYE, walker.pos.z);
    HoloV3 fwd = hv3(sinf(yaw) * cosf(pitch), sinf(pitch),
                     cosf(yaw) * cosf(pitch));
    return holo_camera_make(eye, hv3_add(eye, fwd), hv3(0, 1, 0),
                            70.0f, 1.0f);
}

static void before_frame(void) {
    float dx, dy;
    holo_input_look(&input, &dx, &dy);
    yaw -= dx * 0.003f;
    pitch -= dy * 0.003f;
    if (pitch > 1.5f) pitch = 1.5f;
    if (pitch < -1.5f) pitch = -1.5f;

    if (holo_input_held(&input, SAPP_KEYCODE_T)) {
        if (!t_was_down) {
            spectral_on = !spectral_on;
        }
        t_was_down = 1;
    } else {
        t_was_down = 0;
    }

    timestep_advance((float)sapp_frame_duration());
    for (int s = 0; s < timestep_steps(); s++) {
        float fx = sinf(yaw), fz = cosf(yaw);
        float wish_f = (float)(holo_input_held(&input, SAPP_KEYCODE_W)
                             - holo_input_held(&input, SAPP_KEYCODE_S));
        float wish_r = (float)(holo_input_held(&input, SAPP_KEYCODE_D)
                             - holo_input_held(&input, SAPP_KEYCODE_A));
        walker.vel.x = (fx * wish_f - fz * wish_r) * WALK_SPEED;
        walker.vel.z = (fz * wish_f + fx * wish_r) * WALK_SPEED;
        if (walker.grounded && holo_input_held(&input, SAPP_KEYCODE_SPACE)) {
            walker.vel.y = JUMP;
        }
        holo_walk_step(&walker, &world, timestep_dt());
    }

    HoloCamera cam = camera();
    holo_gpu_scene_fill(&gpu, &scene, &cam, spectral_on);
}

static void after_frame(void) {
    frames_drawn++;
    if (diff_mode && frames_drawn == 5) {
        HoloCamera fixed = camera();
        fixed = holo_camera_make(fixed.pos,
                                 hv3_add(fixed.pos, fixed.forward),
                                 hv3(0, 1, 0), 70.0f,
                                 (float)sapp_width() / (float)sapp_height());
        HoloOracleStats st;
        int ok = holo_oracle_diff(&scene, &fixed, spectral_on, &st);
        printf("DIFF %s: %dx%d, mean err %.4f/255, max %d/255, "
               "%.3f%% pixels off by >8\n",
               ok ? "OK" : "FAIL", st.width, st.height, st.mean, st.max,
               st.outlier_pct);
        exit(ok ? 0 : 1);
    }
}

static void event_cb(const struct sapp_event *ev) {
    holo_input_event(&input, ev);
}

sapp_desc sokol_main(int argc, char *argv[]) {
    diff_mode = argc > 1 && strcmp(argv[1], "--diff") == 0;

    scene = (HoloScene){
        .rects = {
            /* The coarse panel: 833 lines/mm, grooves vertical (90 degrees
               from edge_u), dispersion horizontal. */
            { .corner = { -4.6f, 0.0f, -4.0f },
              .edge_u = { 4.2f, 0.0f, 0.0f },
              .edge_v = { 0.0f, 3.0f, 0.0f },
              .albedo = { 1, 1, 1 },
              .grating_period = 1.2f, .grating_angle = 0.5f * PI,
              .order_w = { 0.28f, 0.16f, 0.28f, 0.16f } },
            /* The fine panel: 1430 lines/mm, same mount -- the same sun
               thrown twice as wide. */
            { .corner = { 0.4f, 0.0f, -4.0f },
              .edge_u = { 4.2f, 0.0f, 0.0f },
              .edge_v = { 0.0f, 3.0f, 0.0f },
              .albedo = { 1, 1, 1 },
              .grating_period = 0.7f, .grating_angle = 0.5f * PI,
              .order_w = { 0.34f, 0.16f, 0.34f, 0.0f } },
        },
        .rect_count = 2,
        .spheres = {
            /* Chrome, for the specular-only comparison. */
            { .center = { -0.1f, 0.55f, -1.2f }, .radius = 0.55f,
              .albedo = { 0.95f, 0.95f, 0.98f }, .mirror = 0.85f },
        },
        .sphere_count = 1,
        .has_floor = 1,
        .floor_y = 0.0f,
        .floor_a = { 0.5f, 0.48f, 0.45f },
        .floor_b = { 0.2f, 0.19f, 0.18f },
        /* The sun behind and above the spawn, so the panels throw its
           orders back at the walker. */
        .sun_dir = { 0.1f, 0.42f, 0.9f },
        .sun_disk_cos = 0.9962f,
        .sun_disk_intensity = 20.0f,
        .horizon = { 0.35f, 0.33f, 0.32f },
        .zenith  = { 0.1f, 0.12f, 0.2f },
    };
    /* Normalize the sun by hand once: |(0.1, 0.42, 0.9)| = 0.998. */
    scene.sun_dir = hv3_norm(scene.sun_dir);


    world = (HoloWalkWorld){
        .radius = 0.3f,
        .height = 1.7f,
        .gravity = 20.0f,
        .floor_y = 0.0f,
        .walls = {
            { .min = { -4.6f, 0, -4.15f }, .max = { 4.6f, 3, -3.95f } },
        },
        .wall_count = 1,
    };
    walker = (HoloWalker){ .pos = hv3(0, 0, 5.0f) };
    timestep_set_hz(120);

    HoloCamera cam = camera();
    holo_gpu_scene_fill(&gpu, &scene, &cam, spectral_on);

    FILE *f = fopen("shaders\\trace.hlsl", "rb");
    if (!f) {
        printf("could not open shaders\\trace.hlsl -- run from the repo root\n");
        exit(2);
    }
    size_t n = fread(shader_src, 1, sizeof shader_src - 1, f);
    shader_src[n] = 0;
    fclose(f);

    return holo_display_app(&(HoloDisplayDesc){
        .title = "hologram m9",
        .fs_source = shader_src,
        .uniforms = &gpu,
        .uniforms_size = sizeof gpu,
        .before_frame = before_frame,
        .after_frame = after_frame,
        .event_cb = event_cb,
    });
}
