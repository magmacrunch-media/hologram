/* M6: polarization -- light picks up a Stokes vector.
 *
 * Two windows onto a bright wall. The left one is a crossed pair of
 * polarizers with a third at 45 degrees slid between their middles: the
 * frame around the strip is black, and the strip GLOWS -- adding a filter
 * brought light back, which no absorption metaphor survives. The right one
 * is the same crossed pair around a full-wave plate: dark at the sodium D
 * line by construction, but retardance runs as 1/lambda, so the other
 * wavelengths leak and the strip shows the interference color every
 * mineralogist knows from crossed-polar microscopy. Nothing in this frame
 * is painted; it is all Mueller matrices and twelve wavelengths.
 *
 *   build\m6_polarization.exe          look at it
 *   build\m6_polarization.exe --diff   hold it to the CPU oracle
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../external/sokol/sokol_app.h"
#include "../../hologram.h"

static HoloGpuScene gpu;
static HoloScene scene;
static char shader_src[65536];
static int diff_mode;
static int dump_mode;
static int frames_drawn;

static const HoloV3 CAM_POS = { 0.0f, 1.8f, 5.2f };
static const HoloV3 CAM_AT  = { 0.0f, 1.8f, 0.0f };

#define DEG (3.14159265f / 180.0f)

static void after_frame(void) {
    frames_drawn++;
    if ((diff_mode || dump_mode) && frames_drawn == 5) {
        HoloCamera cam = holo_camera_make(
            CAM_POS, CAM_AT, hv3(0, 1, 0), 58.0f,
            (float)sapp_width() / (float)sapp_height());
        HoloOracleStats st;
        if (dump_mode) {
            exit(holo_oracle_dump(&scene, &cam, 1, &gpu,
                                  sizeof gpu, "m6_polarization") ? 0 : 1);
        }
        int ok = holo_oracle_diff(&scene, &cam, 1, &st);
        printf("DIFF %s: %dx%d, mean err %.4f/255, max %d/255, "
               "%.3f%% pixels off by >8\n",
               ok ? "OK" : "FAIL", st.width, st.height, st.mean, st.max,
               st.outlier_pct);
        exit(ok ? 0 : 1);
    }
}

/* A filter pane facing the camera. */
static HoloRect filter(float x0, float x1, float y0, float y1, float z,
                       int kind, float angle_deg, float retard) {
    return (HoloRect){
        .corner = hv3(x0, y0, z),
        .edge_u = hv3(x1 - x0, 0, 0),
        .edge_v = hv3(0, y1 - y0, 0),
        .albedo = hv3(1, 1, 1),
        .filter = kind,
        .filter_angle = angle_deg * DEG,
        .retard = retard,
    };
}

sapp_desc sokol_main(int argc, char *argv[]) {
    diff_mode = argc > 1 && strcmp(argv[1], "--diff") == 0;
    dump_mode = argc > 1 && strcmp(argv[1], "--dump") == 0;

    scene = (HoloScene){
        .rects = {
            /* The lit wall everything is read against. */
            { .corner = hv3(-6, 0, -2), .edge_u = hv3(12, 0, 0),
              .edge_v = hv3(0, 4.2f, 0), .albedo = hv3(1, 1, 1) },

            /* Left window: crossed pair, paradox strip between. */
            filter(-4.3f, -0.5f, 0.7f, 3.1f, 0.8f, HOLO_POLARIZER, 0, 0),
            filter(-4.3f, -0.5f, 0.7f, 3.1f, 0.0f, HOLO_POLARIZER, 90, 0),
            filter(-3.3f, -1.5f, 1.0f, 2.8f, 0.4f, HOLO_POLARIZER, 45, 0),

            /* Right window: crossed pair, full-wave plate between. */
            filter(0.5f, 4.3f, 0.7f, 3.1f, 0.8f, HOLO_POLARIZER, 0, 0),
            filter(0.5f, 4.3f, 0.7f, 3.1f, 0.0f, HOLO_POLARIZER, 90, 0),
            filter(1.5f, 3.3f, 1.0f, 2.8f, 0.4f, HOLO_WAVEPLATE, 45,
                   2.0f * 3.14159265f),
        },
        .rect_count = 7,
        .has_floor = 1,
        .floor_y = 0.0f,
        .floor_a = { 0.4f, 0.4f, 0.42f },
        .floor_b = { 0.15f, 0.15f, 0.17f },
        /* Sun toward the wall, so the wall is the bright source the
           filters dissect. */
        .sun_dir = { 0.169f, 0.507f, 0.845f },
        .horizon = { 0.75f, 0.72f, 0.7f },
        .zenith  = { 0.2f, 0.3f, 0.5f },
    };
    HoloCamera cam = holo_camera_make(CAM_POS, CAM_AT, hv3(0, 1, 0),
                                      58.0f, 1.0f);
    holo_gpu_scene_fill(&gpu, &scene, &cam, 1);

    if (!holo_load_shader(shader_src, (int)sizeof shader_src)) {
        exit(2);
    }

    return holo_display_app(&(HoloDisplayDesc){
        .title = "hologram m6",
        .fs_source = shader_src,
        .uniforms = &gpu,
        .uniforms_size = sizeof gpu,
        .after_frame = after_frame,
    });
}
