/* M2: the tracer on the GPU, held to the oracle.
 *
 * The same scene m1_cpu renders, traced by shaders/trace.hlsl on the
 * fullscreen quad. Run bare it is just a window showing the frame; run as
 *
 *   build\m2_gpu.exe --diff
 *
 * it renders a few frames, then holds the GPU's pixels to the CPU oracle;
 * the exit code is the verdict, so a script can run the comparison
 * mechanically.
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

static void after_frame(void) {
    frames_drawn++;
    /* A few frames in, so the swapchain is past its first-present wrinkles. */
    if (diff_mode && frames_drawn == 5) {
        HoloCamera cam = holo_camera_make(
            hv3(0, 1.6f, 6), hv3(0, 0.8f, 0), hv3(0, 1, 0), 55.0f,
            (float)sapp_width() / (float)sapp_height());
        HoloOracleStats st;
        int ok = holo_oracle_diff(&scene, &cam, 0, &st);
        printf("DIFF %s: %dx%d, mean err %.4f/255, max %d/255, "
               "%.3f%% pixels off by >8\n",
               ok ? "OK" : "FAIL", st.width, st.height, st.mean, st.max,
               st.outlier_pct);
        /* exit() rather than a polite quit: the verdict IS the program, and
           the device teardown we skip matters to nobody at process end. */
        exit(ok ? 0 : 1);
    }
}

sapp_desc sokol_main(int argc, char *argv[]) {
    diff_mode = argc > 1 && strcmp(argv[1], "--diff") == 0;

    scene = (HoloScene){
        .spheres = {
            { .center = { 0.0f, 1.0f, 0.0f },  .radius = 1.0f,
              .albedo = { 0.85f, 0.25f, 0.35f } },
            { .center = { -2.2f, 0.6f, 1.0f }, .radius = 0.6f,
              .albedo = { 0.25f, 0.55f, 0.85f } },
            { .center = { 1.9f, 0.45f, 1.4f }, .radius = 0.45f,
              .albedo = { 0.9f, 0.75f, 0.25f } },
        },
        .sphere_count = 3,
        .has_floor = 1,
        .floor_y = 0.0f,
        .floor_a = { 0.85f, 0.85f, 0.85f },
        .floor_b = { 0.25f, 0.3f, 0.35f },
        .sun_dir = { -3.0f / 7, 6.0f / 7, 2.0f / 7 },
        .horizon = { 1.0f, 0.9f, 0.8f },
        .zenith  = { 0.25f, 0.45f, 0.9f },
    };
    /* Aspect is derived from res in the shader; 1.0 here is a placeholder. */
    HoloCamera cam = holo_camera_make(hv3(0, 1.6f, 6), hv3(0, 0.8f, 0),
                                      hv3(0, 1, 0), 55.0f, 1.0f);
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
        .title = "hologram m2",
        .fs_source = shader_src,
        .uniforms = &gpu,
        .uniforms_size = sizeof gpu,
        .after_frame = after_frame,
    });
}
