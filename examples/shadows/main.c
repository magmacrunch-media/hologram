/* shadows -- what blocks the sun here, and what deliberately does not.
 *
 * Not a milestone; those are m0 through m9. This one exists because
 * sun_blocked() is the engine's most-duplicated decision -- written four
 * times, once in cpu_trace.c and once per shader dialect -- and until this
 * example there was no frame in which getting it wrong showed up.
 *
 * That is not hypothetical. Dishes arrived in M8 and never reached
 * sun_blocked in any of the four, so a curved mirror let the sun straight
 * through it onto the ground for a whole release. m8_furnace could not
 * see it: its dishes hang over floor the camera barely takes in, so
 * deleting the dish loop from a dialect moves its mean error from
 * 0.0191/255 to 0.0289 and 0.022% of pixels to 0.087% -- both inside the
 * verdict's thresholds, and the diff goes on saying OK.
 *
 * This frame is arranged to be loud instead. The same mutation here:
 *
 *     intact   mean 0.0070/255, max  60/255, 0.021% off by >8   OK
 *     no dish  mean 1.2491/255, max 130/255, 2.984% off by >8   FAIL
 *
 * -- past the mean bar of 1.0 and four times past the outlier bar of
 * 0.75%. Four casters and non-casters stand in a row on plain floor with
 * the sun behind them, so every shadow is thrown forward into open frame
 * rather than hiding behind the thing that cast it:
 *
 *     dish          opaque, mirror or matte and never glass -- casts
 *     matte sphere  the ordinary case, and the control for the rest
 *     glass sphere  casts NOTHING, on purpose
 *     panel         an opaque rect -- casts
 *
 * The glass is not an oversight. Mostly-clear glass throws no hard shadow
 * because without caustics a black disc under a glass ball is more wrong
 * than no shadow at all: the ball's business is bending light, and a
 * shadow ray stopping at it would be claiming it swallowed the light
 * instead. tests/test_trace.c pins that.
 *
 * A polarizer is excused for the same reason plus one more -- half
 * absorbing the sun on the way in would double-count what Malus already
 * does at the surface -- and it is NOT in this scene, for a reason worth
 * recording. In the RGB path the two GPU dialects draw a polarizer black
 * where cpu_trace.c draws it at 50%. Standing one here costs 0.841% of
 * pixels and fails the diff over something with nothing to do with
 * shadows. m6_polarization cannot see it either: it diffs with
 * spectral = 1, and the approximation only exists in the RGB walk.
 *
 *   build\shadows.exe          look at it
 *   build\shadows.exe --diff   hold it to the CPU oracle
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

/* High and back, so the floor -- where the whole point lands -- takes most
   of the frame rather than the horizon. */
static const HoloV3 CAM_POS = { 0.0f, 5.6f, 10.2f };
static const HoloV3 CAM_AT  = { 0.0f, 0.6f, 0.4f };

static void after_frame(void) {
    frames_drawn++;
    if ((diff_mode || dump_mode) && frames_drawn == 5) {
        HoloCamera cam = holo_camera_make(
            CAM_POS, CAM_AT, hv3(0, 1, 0), 62.0f,
            (float)sapp_width() / (float)sapp_height());
        HoloOracleStats st;
        if (dump_mode) {
            exit(holo_oracle_dump(&scene, &cam, 0, &gpu,
                                  sizeof gpu, "shadows") ? 0 : 1);
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

    scene = (HoloScene){
        .spheres = {
            /* Matte: the shadow everyone expects, and the one the others
               are read against. */
            { .center = { -1.4f, 0.9f, 0.0f }, .radius = 0.9f,
              .albedo = { 0.85f, 0.35f, 0.32f } },
            /* Clear glass: bends the floor through itself, and leaves the
               ground under it lit. */
            { .center = { 0.7f, 0.75f, 0.0f }, .radius = 0.75f,
              .albedo = { 1.0f, 1.0f, 1.0f },
              .transmit = 1.0f, .ior = 1.5f },
        },
        .sphere_count = 2,
        .rects = {
            /* An opaque standing panel: casts a parallelogram. */
            { .corner = { 1.9f, 0.0f, -0.15f },
              .edge_u = { 1.3f, 0.0f, 0.0f },
              .edge_v = { 0.0f, 1.7f, 0.0f },
              .albedo = { 0.55f, 0.58f, 0.62f } },
        },
        .rect_count = 1,
        .dishes = {
            /* A shallow matte paraboloid, wide enough that a dialect
               forgetting it moves thousands of pixels by most of the
               floor's albedo. K = -1 and sag at the rim r^2/2R = 0.15, so
               it reads as a dish rather than a disc. */
            { .apex = { -4.0f, 2.3f, 0.0f },
              .axis = { 0.0f, 1.0f, 0.0f },
              .curv_r = 11.0f, .conic_k = -1.0f, .rim = 1.8f,
              .albedo = { 0.88f, 0.88f, 0.92f } },
        },
        .dish_count = 1,
        .has_floor = 1,
        .floor_y = 0.0f,
        /* A quiet checker: enough to read as ground, not enough to fight
           the shadows for attention. */
        .floor_a = { 0.78f, 0.76f, 0.72f },
        .floor_b = { 0.66f, 0.64f, 0.60f },
        /* Behind the row and half way up the sky: every shadow is thrown
           toward the camera instead of hiding behind its own object, which
           is the accident that made m8_furnace blind to this. */
        .sun_dir = { 0.2489f, 0.7966f, -0.5504f },
        .horizon = { 1.0f, 0.92f, 0.82f },
        .zenith  = { 0.3f, 0.5f, 0.85f },
    };
    HoloCamera cam = holo_camera_make(CAM_POS, CAM_AT, hv3(0, 1, 0),
                                      62.0f, 1.0f);
    holo_gpu_scene_fill(&gpu, &scene, &cam, 0);

    if (!holo_load_shader(shader_src, (int)sizeof shader_src)) {
        exit(2);
    }

    return holo_display_app(&(HoloDisplayDesc){
        .title = "hologram shadows",
        .fs_source = shader_src,
        .uniforms = &gpu,
        .uniforms_size = sizeof gpu,
        .after_frame = after_frame,
    });
}
