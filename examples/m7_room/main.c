/* M7: the vertical slice -- a room made of the engine's whole vocabulary,
 * and you can walk around in it.
 *
 * Two facing mirror walls recursing, a glass window in the back wall, a
 * flint ball fringing the world behind it, a polarizer pane you can look
 * through, a checker floor with a sheen -- all traced spectrally with
 * polarization, live, while WASD and the mouse move a fixed-step walker
 * with real collision. This is the frame Crystal Mirror Maze gets rebuilt
 * on.
 *
 *   build\m7_room.exe          click to look; WASD walk, Space jump,
 *                              T toggles spectral, Escape frees the mouse
 *   build\m7_room.exe --diff   hold the still frame to the CPU oracle
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
static float yaw = PI;            /* facing -z, into the room */
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
    /* Mouse look, per frame. */
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

    /* The walk, in fixed steps however the frames arrive. */
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
        /* The walker has not been told to move, so the still frame is
           deterministic. */
        HoloCamera cam = camera();
        cam = holo_camera_make(cam.pos,
                               hv3_add(cam.pos, cam.forward), hv3(0, 1, 0),
                               70.0f,
                               (float)sapp_width() / (float)sapp_height());
        HoloOracleStats st;
        if (dump_mode) {
            exit(holo_oracle_dump(&scene, &cam, spectral_on, &gpu,
                                  sizeof gpu, "m7_room") ? 0 : 1);
        }
        int ok = holo_oracle_diff(&scene, &cam, spectral_on, &st);
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

    /* The room: 8m wide (mirror to mirror), 12m long, open to the sky. */
    scene = (HoloScene){
        .spheres = {
            /* The flint ball, center stage. */
            { .center = { 0.0f, 1.0f, -2.0f }, .radius = 1.0f,
              .albedo = { 1, 1, 1 }, .transmit = 1.0f,
              .ior = 1.62f, .disperse = 0.02f },
            /* Chrome. */
            { .center = { -2.2f, 0.6f, -4.0f }, .radius = 0.6f,
              .albedo = { 0.95f, 0.95f, 0.98f }, .mirror = 0.85f },
            /* Matte red, so the mirrors have something warm to repeat. */
            { .center = { 2.3f, 0.5f, -3.2f }, .radius = 0.5f,
              .albedo = { 0.85f, 0.25f, 0.35f } },
        },
        .sphere_count = 3,
        .rects = {
            /* West and east walls: the facing mirrors. */
            { .corner = { -4, 0, -6 }, .edge_u = { 0, 0, 12 },
              .edge_v = { 0, 3, 0 },
              .albedo = { 0.88f, 0.92f, 0.95f }, .mirror = 1.0f },
            { .corner = { 4, 0, -6 }, .edge_u = { 0, 0, 12 },
              .edge_v = { 0, 3, 0 },
              .albedo = { 0.88f, 0.92f, 0.95f }, .mirror = 1.0f },
            /* North wall in two matte pieces with a glass window between. */
            { .corner = { -4, 0, -6 }, .edge_u = { 3, 0, 0 },
              .edge_v = { 0, 3, 0 }, .albedo = { 0.75f, 0.7f, 0.62f } },
            { .corner = { 1, 0, -6 }, .edge_u = { 3, 0, 0 },
              .edge_v = { 0, 3, 0 }, .albedo = { 0.75f, 0.7f, 0.62f } },
            { .corner = { -1, 0, -6 }, .edge_u = { 2, 0, 0 },
              .edge_v = { 0, 3, 0 }, .albedo = { 1, 1, 1 },
              .transmit = 1.0f, .ior = 1.5f },
            /* South wall, behind the spawn. */
            { .corner = { -4, 0, 6 }, .edge_u = { 8, 0, 0 },
              .edge_v = { 0, 3, 0 }, .albedo = { 0.6f, 0.62f, 0.66f } },
            /* A freestanding polarizer pane to peer through. */
            { .corner = { 0.8f, 0, 0.6f }, .edge_u = { 2.0f, 0, 0 },
              .edge_v = { 0, 2.4f, 0 }, .albedo = { 1, 1, 1 },
              .filter = HOLO_POLARIZER, .filter_angle = 0.0f },
        },
        .rect_count = 7,
        .has_floor = 1,
        .floor_y = 0.0f,
        .floor_a = { 0.8f, 0.8f, 0.82f },
        .floor_b = { 0.22f, 0.26f, 0.3f },
        .floor_mirror = 0.12f,
        .sun_dir = { 0.169f, 0.507f, 0.845f },
        .horizon = { 0.95f, 0.9f, 0.85f },
        .zenith  = { 0.3f, 0.45f, 0.75f },
    };

    world = (HoloWalkWorld){
        .radius = 0.3f,
        .height = 1.7f,
        .gravity = 20.0f,
        .floor_y = 0.0f,
        .walls = {
            { .min = { -4.3f, 0, -6.3f }, .max = { -4.0f, 3, 6.3f } },
            { .min = { 4.0f, 0, -6.3f },  .max = { 4.3f, 3, 6.3f } },
            { .min = { -4.3f, 0, -6.3f }, .max = { 4.3f, 3, -6.0f } },
            { .min = { -4.3f, 0, 6.0f },  .max = { 4.3f, 3, 6.3f } },
            /* The polarizer pane is thin but solid. */
            { .min = { 0.8f, 0, 0.55f },  .max = { 2.8f, 2.4f, 0.65f } },
        },
        .wall_count = 5,
    };
    walker = (HoloWalker){ .pos = hv3(0, 0, 4.5f) };
    timestep_set_hz(120);

    HoloCamera cam = camera();
    holo_gpu_scene_fill(&gpu, &scene, &cam, spectral_on);

    if (!holo_load_shader(shader_src, (int)sizeof shader_src)) {
        exit(2);
    }

    return holo_display_app(&(HoloDisplayDesc){
        .title = "hologram m7",
        .fs_source = shader_src,
        .uniforms = &gpu,
        .uniforms_size = sizeof gpu,
        .before_frame = before_frame,
        .after_frame = after_frame,
        .event_cb = event_cb,
    });
}
