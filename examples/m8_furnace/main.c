/* M8: curved mirrors -- a solar furnace you can jump into.
 *
 * The big dish is a true paraboloid (K = -1) aimed at a low sun, and the
 * sky now carries the sun as a disk. Stand anywhere and the dish shows a
 * warped world; its focus is parked at eye height on the path from spawn,
 * so walking forward carries you through it -- and as you cross, every
 * zone of the dish reflects your eye straight into the sun and the whole
 * aperture flashes blinding white, which is exactly what standing at the
 * focus of a real solar furnace looks like (do not). The small dish
 * beside the path is a concave shaving mirror: the world in it hangs
 * upside down.
 *
 *   build\m8_furnace.exe          WASD walk, Space jump, mouse look,
 *                                 T toggles spectral
 *   build\m8_furnace.exe --diff   hold the still frame to the CPU oracle
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
static int dump_mode;
static int frames_drawn;
static int spectral_on = 1;
static int t_was_down;
static float yaw = PI;            /* facing -z, toward the dish */
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
    if ((diff_mode || dump_mode) && frames_drawn == 5) {
        HoloCamera fixed = camera();
        fixed = holo_camera_make(fixed.pos,
                                 hv3_add(fixed.pos, fixed.forward),
                                 hv3(0, 1, 0), 70.0f,
                                 (float)sapp_width() / (float)sapp_height());
        HoloOracleStats st;
        if (dump_mode) {
            exit(holo_oracle_dump(&scene, &fixed, spectral_on, &gpu,
                                  sizeof gpu, "m8_furnace") ? 0 : 1);
        }
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
    dump_mode = argc > 1 && strcmp(argv[1], "--dump") == 0;

    /* A low sun, so the furnace dish stands nearly upright. */
    HoloV3 sun = hv3_norm(hv3(0.2f, 0.25f, 0.95f));

    scene = (HoloScene){
        .dishes = {
            /* The furnace: f = R/2 = 1.25m, aimed dead at the sun, and the
               focus parked at standing eye height exactly on the walk line
               from spawn -- walk forward and you pass through it. */
            { .apex = { 0.05242f, 1.24054f, -0.81461f },
              .axis = { 0.19806f, 0.24757f, 0.94849f },
              .curv_r = 2.5f, .conic_k = -1.0f, .rim = 1.2f,
              .albedo = { 0.94f, 0.95f, 0.97f }, .mirror = 1.0f },
            /* A concave shaving mirror at eye height by the path: past
               its center of curvature the world in it hangs upside down. */
            { .apex = { 3.0f, 1.5f, -1.0f },
              .axis = { -0.44262f, 0.00738f, 0.88524f },
              .curv_r = 1.5f, .conic_k = 0.0f, .rim = 0.6f,
              .albedo = { 0.94f, 0.95f, 0.97f }, .mirror = 1.0f },
        },
        .dish_count = 2,
        .spheres = {
            { .center = { -2.6f, 0.55f, -0.5f }, .radius = 0.55f,
              .albedo = { 0.85f, 0.25f, 0.35f } },
            { .center = { -1.8f, 0.4f, 1.6f },   .radius = 0.4f,
              .albedo = { 0.95f, 0.95f, 0.98f }, .mirror = 0.85f },
        },
        .sphere_count = 2,
        .has_floor = 1,
        .floor_y = 0.0f,
        .floor_a = { 0.75f, 0.72f, 0.66f },
        .floor_b = { 0.3f, 0.28f, 0.26f },
        .sun_dir = { 0.19806f, 0.24757f, 0.94849f },
        .sun_disk_cos = 0.9962f,   /* a fat 5-degree demo sun: the flash
                                      region at the focus is f*halfangle,
                                      about 11cm -- walkable, not surgical */
        .sun_disk_intensity = 30.0f,
        .horizon = { 1.0f, 0.85f, 0.7f },
        .zenith  = { 0.25f, 0.4f, 0.7f },
    };
    (void)sun;

    world = (HoloWalkWorld){
        .radius = 0.3f,
        .height = 1.7f,
        .gravity = 20.0f,
        .floor_y = 0.0f,
        .wall_count = 0,       /* an open field */
    };
    walker = (HoloWalker){ .pos = hv3(0.3f, 0, 4.0f) };
    timestep_set_hz(120);

    HoloCamera cam = camera();
    holo_gpu_scene_fill(&gpu, &scene, &cam, spectral_on);

    if (!holo_load_shader(shader_src, (int)sizeof shader_src)) {
        exit(2);
    }

    return holo_display_app(&(HoloDisplayDesc){
        .title = "hologram m8",
        .fs_source = shader_src,
        .uniforms = &gpu,
        .uniforms_size = sizeof gpu,
        .before_frame = before_frame,
        .after_frame = after_frame,
        .event_cb = event_cb,
    });
}
