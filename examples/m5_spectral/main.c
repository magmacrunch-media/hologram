/* M5: spectral light -- the first thing no shipping game engine does.
 *
 * Every pixel traces twelve wavelengths, each refracting at its own
 * n(lambda) by Cauchy's equation, folded to sRGB through the CIE color
 * matching functions. The flint ball smears the white stripe behind it
 * into a real spectrum, and the crown ball beside it fringes gently,
 * because flint disperses five times harder than crown -- the same reason
 * camera lenses pair the two. Nothing here is a rainbow texture; the
 * colors exist because blue light actually bends more than red.
 *
 *   build\m5_spectral.exe          look at it
 *   build\m5_spectral.exe --diff   hold it to the CPU oracle (spectral CPU
 *                                  rendering takes several seconds; wait)
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

static const HoloV3 CAM_POS = { 0.0f, 1.25f, 4.4f };
static const HoloV3 CAM_AT  = { 0.0f, 1.0f, 0.0f };

static void after_frame(void) {
    frames_drawn++;
    if (diff_mode && frames_drawn == 5) {
        HoloCamera cam = holo_camera_make(
            CAM_POS, CAM_AT, hv3(0, 1, 0), 50.0f,
            (float)sapp_width() / (float)sapp_height());
        HoloOracleStats st;
        int ok = holo_oracle_diff(&scene, &cam, 1, &st);
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
            /* Dense flint, front and center: ior 1.62 with a Cauchy B a
               few times BK7's. This is the prism of the piece. */
            { .center = { 0.55f, 1.0f, 0.0f },  .radius = 1.0f,
              .albedo = { 1.0f, 1.0f, 1.0f },
              .transmit = 1.0f, .ior = 1.62f, .disperse = 0.03f },
            /* Crown glass (BK7 to the decimal) for comparison: same shot,
               a fifth the dispersion. */
            { .center = { -1.7f, 0.7f, 0.3f }, .radius = 0.7f,
              .albedo = { 1.0f, 1.0f, 1.0f },
              .transmit = 1.0f, .ior = 1.5168f, .disperse = 0.0042f },
        },
        .sphere_count = 2,
        .rects = {
            /* A near-black backdrop with a white stripe at ball height:
               the high-contrast edge that dispersion tears into color. */
            { .corner = { -6.0f, 0.0f, -3.0f },
              .edge_u = { 12.0f, 0.0f, 0.0f },
              .edge_v = { 0.0f, 4.0f, 0.0f },
              .albedo = { 0.06f, 0.06f, 0.07f } },
            { .corner = { -6.0f, 0.82f, -2.98f },
              .edge_u = { 12.0f, 0.0f, 0.0f },
              .edge_v = { 0.0f, 0.36f, 0.0f },
              .albedo = { 1.0f, 1.0f, 1.0f } },
        },
        .rect_count = 2,
        .has_floor = 1,
        .floor_y = 0.0f,
        .floor_a = { 0.5f, 0.5f, 0.52f },
        .floor_b = { 0.12f, 0.12f, 0.14f },
        /* The sun leans toward the backdrop so the stripe reads bright --
           it is the light source the dispersion dissects. (1,3,5)/sqrt(35). */
        .sun_dir = { 0.169f, 0.507f, 0.845f },
        .horizon = { 0.9f, 0.85f, 0.8f },
        .zenith  = { 0.15f, 0.25f, 0.5f },
    };
    HoloCamera cam = holo_camera_make(CAM_POS, CAM_AT, hv3(0, 1, 0),
                                      50.0f, 1.0f);
    holo_gpu_scene_fill(&gpu, &scene, &cam, 1);

    FILE *f = fopen("shaders\\trace.hlsl", "rb");
    if (!f) {
        printf("could not open shaders\\trace.hlsl -- run from the repo root\n");
        exit(2);
    }
    size_t n = fread(shader_src, 1, sizeof shader_src - 1, f);
    shader_src[n] = 0;
    fclose(f);

    return holo_display_app(&(HoloDisplayDesc){
        .title = "hologram m5",
        .fs_source = shader_src,
        .uniforms = &gpu,
        .uniforms_size = sizeof gpu,
        .after_frame = after_frame,
    });
}
