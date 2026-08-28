/* M4: glass -- refraction, TIR, and the real Fresnel equations.
 *
 * A clear glass ball bending the checker behind it, a tinted one coloring
 * it, a chrome sphere for company, and a standing pane that reflects more
 * the more obliquely you look through it, because Fresnel says so. No
 * parameter fakes any of this: the glass goes mirror-like at grazing
 * incidence out of the same two equations the tests hold to Brewster's
 * angle.
 *
 *   build\m4_glass.exe          look at it
 *   build\m4_glass.exe --diff   hold it to the CPU oracle
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
static int frames_drawn;

static const HoloV3 CAM_POS = { 0.2f, 1.5f, 5.6f };
static const HoloV3 CAM_AT  = { 0.0f, 0.9f, 0.0f };

static void after_frame(void) {
    frames_drawn++;
    if (diff_mode && frames_drawn == 5) {
        HoloCamera cam = holo_camera_make(
            CAM_POS, CAM_AT, hv3(0, 1, 0), 55.0f,
            (float)sapp_width() / (float)sapp_height());
        HoloOracleStats st;
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

    scene = (HoloScene){
        .spheres = {
            /* Clear glass, dead center: the checker bends through it. */
            { .center = { 0.0f, 1.0f, 0.0f },   .radius = 1.0f,
              .albedo = { 1.0f, 1.0f, 1.0f },
              .transmit = 1.0f, .ior = 1.5f },
            /* Tinted glass: same physics, colored throughput. */
            { .center = { -2.1f, 0.65f, 0.9f }, .radius = 0.65f,
              .albedo = { 0.6f, 0.8f, 1.0f },
              .transmit = 1.0f, .ior = 1.5f },
            /* Chrome, for the company mirrors keep. */
            { .center = { 2.0f, 0.55f, 0.7f },  .radius = 0.55f,
              .albedo = { 0.95f, 0.95f, 0.98f }, .mirror = 0.85f },
            /* Matte red behind the pane, to be seen through it. */
            { .center = { 1.3f, 0.4f, -2.2f },  .radius = 0.4f,
              .albedo = { 0.85f, 0.25f, 0.35f } },
        },
        .sphere_count = 4,
        .rects = {
            /* A standing window: straight through at the center of view,
               increasingly a mirror toward grazing angles. */
            { .corner = { 0.6f, 0.0f, -1.4f },
              .edge_u = { 2.6f, 0.0f, 0.0f },
              .edge_v = { 0.0f, 2.2f, 0.0f },
              .albedo = { 1.0f, 1.0f, 1.0f },
              .transmit = 1.0f, .ior = 1.5f },
        },
        .rect_count = 1,
        .has_floor = 1,
        .floor_y = 0.0f,
        .floor_a = { 0.85f, 0.85f, 0.85f },
        .floor_b = { 0.25f, 0.3f, 0.35f },
        .sun_dir = { -3.0f / 7, 6.0f / 7, 2.0f / 7 },
        .horizon = { 1.0f, 0.9f, 0.8f },
        .zenith  = { 0.25f, 0.45f, 0.9f },
    };
    HoloCamera cam = holo_camera_make(CAM_POS, CAM_AT, hv3(0, 1, 0),
                                      55.0f, 1.0f);
    holo_gpu_scene_fill(&gpu, &scene, &cam, 0);

    FILE *f = fopen("shaders\\trace.hlsl", "rb");
    if (!f) {
        printf("could not open shaders\\trace.hlsl -- run from the repo root\n");
        exit(2);
    }
    size_t n = fread(shader_src, 1, sizeof shader_src - 1, f);
    shader_src[n] = 0;
    fclose(f);

    return holo_display_app(&(HoloDisplayDesc){
        .title = "hologram m4",
        .fs_source = shader_src,
        .uniforms = &gpu,
        .uniforms_size = sizeof gpu,
        .after_frame = after_frame,
    });
}
