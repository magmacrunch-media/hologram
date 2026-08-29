/* M3: the infinite corridor -- the shot this engine exists for.
 *
 * Two facing mirror panels with the scene between them, traced to real
 * recursion depth: the corridor repeats until the mirrors' own tint runs
 * the light out, exactly as it does between two real mirrors. This is the
 * frame screen-space reflection cannot draw (it can only reflect what is
 * already on screen), and the reason Crystal Mirror Maze needs a ray
 * tracer under it.
 *
 *   build\m3_mirrors.exe          look at it
 *   build\m3_mirrors.exe --diff   hold it to the CPU oracle
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../external/sokol/sokol_app.h"
#include "../../hologram.h"

static HoloGpuScene gpu;
static HoloScene scene;
static char shader_src[32768];
static int diff_mode;
static int dump_mode;
static int frames_drawn;

static const HoloV3 CAM_POS = { 0.0f, 1.5f, 5.4f };
static const HoloV3 CAM_AT  = { -1.3f, 1.15f, -8.0f };

static void after_frame(void) {
    frames_drawn++;
    if ((diff_mode || dump_mode) && frames_drawn == 5) {
        HoloCamera cam = holo_camera_make(
            CAM_POS, CAM_AT, hv3(0, 1, 0), 60.0f,
            (float)sapp_width() / (float)sapp_height());
        HoloOracleStats st;
        if (dump_mode) {
            exit(holo_oracle_dump(&scene, &cam, 0, &gpu,
                                  sizeof gpu, "m3_mirrors") ? 0 : 1);
        }
        int ok = holo_oracle_diff(&scene, &cam, 0, &st);
        printf("DIFF %s: %dx%d, mean err %.4f/255, max %d/255, "
               "%.3f%% pixels off by >8\n",
               ok ? "OK" : "FAIL", st.width, st.height, st.mean, st.max,
               st.outlier_pct);
        exit(ok ? 0 : 1);
    }
}

sapp_desc sokol_main(int argc, char *argv[]) {
    diff_mode = argc > 1 && strcmp(argv[1], "--diff") == 0;
    dump_mode = argc > 1 && strcmp(argv[1], "--dump") == 0;

    /* A corridor along z: mirror walls at x = -2.5 and x = +2.5, open to
       the sky, a polished checker floor, and three spheres to multiply. */
    scene = (HoloScene){
        .spheres = {
            { .center = { -0.9f, 1.0f, -1.0f }, .radius = 1.0f,
              .albedo = { 0.85f, 0.25f, 0.35f } },
            { .center = { 1.1f, 0.55f, 0.6f },  .radius = 0.55f,
              .albedo = { 0.95f, 0.95f, 0.98f }, .mirror = 0.85f },
            { .center = { 0.2f, 0.4f, 1.9f },   .radius = 0.4f,
              .albedo = { 0.9f, 0.75f, 0.25f } },
        },
        .sphere_count = 3,
        .rects = {
            /* Left wall: from (-2.5, 0, -8) running 14m along z, 3.2m up. */
            { .corner = { -2.5f, 0.0f, -8.0f },
              .edge_u = { 0.0f, 0.0f, 14.0f },
              .edge_v = { 0.0f, 3.2f, 0.0f },
              .albedo = { 0.88f, 0.92f, 0.95f }, .mirror = 1.0f },
            /* Right wall, facing it. */
            { .corner = { 2.5f, 0.0f, -8.0f },
              .edge_u = { 0.0f, 0.0f, 14.0f },
              .edge_v = { 0.0f, 3.2f, 0.0f },
              .albedo = { 0.88f, 0.92f, 0.95f }, .mirror = 1.0f },
            /* A matte back wall so the corridor ends in a wall, not sky. */
            { .corner = { -2.5f, 0.0f, -8.0f },
              .edge_u = { 5.0f, 0.0f, 0.0f },
              .edge_v = { 0.0f, 3.2f, 0.0f },
              .albedo = { 0.75f, 0.7f, 0.62f } },
        },
        .rect_count = 3,
        .has_floor = 1,
        .floor_y = 0.0f,
        .floor_a = { 0.85f, 0.85f, 0.85f },
        .floor_b = { 0.25f, 0.3f, 0.35f },
        .floor_mirror = 0.15f,
        .sun_dir = { -3.0f / 7, 6.0f / 7, 2.0f / 7 },
        .horizon = { 1.0f, 0.9f, 0.8f },
        .zenith  = { 0.25f, 0.45f, 0.9f },
    };
    HoloCamera cam = holo_camera_make(CAM_POS, CAM_AT, hv3(0, 1, 0),
                                      60.0f, 1.0f);
    holo_gpu_scene_fill(&gpu, &scene, &cam, 0);

    if (!holo_load_shader(shader_src, (int)sizeof shader_src)) {
        exit(2);
    }

    return holo_display_app(&(HoloDisplayDesc){
        .title = "hologram m3",
        .fs_source = shader_src,
        .uniforms = &gpu,
        .uniforms_size = sizeof gpu,
        .after_frame = after_frame,
    });
}
