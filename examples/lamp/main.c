/* lamp -- a sun with a colour, and a sky that lights what the sun cannot.
 *
 * Not a milestone. It exists for the reason `shadows` does: two scene
 * fields, sun_color and sky_light, are stated four times over (cpu_trace.c
 * and once per shader dialect), every older example leaves both at their
 * defaults, and a default is exactly the value under which a dialect that
 * forgot the field still passes. With both unset all eleven older examples
 * diff to the digit they did before the fields existed, which is the
 * promise -- and also means none of them would notice.
 *
 * So this frame sets both, and is arranged so that each shows:
 *
 *     the back wall    stands vertical under a lamp that is straight
 *                      overhead, so the sun gives it nothing at all. Under
 *                      the flat ambient stand-in it would be a tenth of its
 *                      albedo whatever the sky was; here it is lit by the
 *                      sky, in the sky's colour, and brighter toward the
 *                      zenith's share than the floor's shadow is.
 *     the side wall    the same, facing the other way: a dome lights a
 *                      vertical surface alike whichever way it faces.
 *     under the slab   a matte slab on four invisible legs. The floor under
 *                      it has lost the sun and kept the sky: UNOCCLUDED is
 *                      the approximation, and this is where it shows.
 *     the white ball   sunlit above, skylit below, amber all the way round.
 *     the grating      leaned at the camera so the lamp's first order comes
 *                      straight down the lens -- 1 um, and the lean solves
 *                      sin(psi - t) - sin(t) = 0.55 for this eye line, so it
 *                      centres on 550 nm. Under a white lamp the band runs
 *                      from violet to red. Under this one the violet end is
 *                      simply absent, because a yellow sun has no 420 to
 *                      467 nm disk to throw: a safe light, by the spectrum
 *                      and not by a tint.
 *
 * Spectral by default, because that is where sun_color is read through
 * holo_albedo_at; --rgb takes the other walk, which multiplies by it, and
 * both are held to the oracle:
 *
 *   build\lamp.exe               look at it
 *   build\lamp.exe --white       the same room under a white lamp, to compare
 *   build\lamp.exe --diff        hold it to the CPU oracle, spectrally
 *   build\lamp.exe --rgb --diff  and through the RGB walk
 *   build\lamp.exe --dump        write the inputs for tools/gldiff
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../external/sokol/sokol_app.h"
#include "../../hologram.h"

static HoloGpuScene gpu;
static HoloScene scene;
static char shader_src[131072];
static int diff_mode, dump_mode;
static int spectral_on = 1;
static int white_mode;
static int frames_drawn;

/* On the grating's own eye line: three times exam distance back along
   (0, 0.45, 0.9) from its centre at (0, 1.1, 0). */
static const HoloV3 CAM_POS = { 0.0f, 2.45f, 2.7f };
static const HoloV3 CAM_AT  = { 0.0f, 1.10f, 0.0f };
#define FOV 58.0f

static void after_frame(void) {
    frames_drawn++;
    if ((diff_mode || dump_mode) && frames_drawn == 5) {
        HoloCamera cam = holo_camera_make(
            CAM_POS, CAM_AT, hv3(0, 1, 0), FOV,
            (float)sapp_width() / (float)sapp_height());
        const char *name = white_mode ? "lamp_white"
                         : spectral_on ? "lamp" : "lamp_rgb";
        HoloOracleStats st;
        if (dump_mode) {
            exit(holo_oracle_dump(&scene, &cam, spectral_on, &gpu,
                                  sizeof gpu, name) ? 0 : 1);
        }
        int ok = holo_oracle_diff(&scene, &cam, spectral_on, &st);
        printf("DIFF %s: %dx%d, mean err %.4f/255, max %d/255, "
               "%.3f%% pixels off by >8\n",
               ok ? "OK" : "FAIL", st.width, st.height, st.mean, st.max,
               st.outlier_pct);
        exit(ok ? 0 : 1);
    }
}

sapp_desc sokol_main(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--diff") == 0) diff_mode = 1;
        if (strcmp(argv[i], "--dump") == 0) dump_mode = 1;
        if (strcmp(argv[i], "--rgb") == 0) spectral_on = 0;
        if (strcmp(argv[i], "--white") == 0) white_mode = 1;
    }

    /* The grating's lean is the root of sin(psi - t) - sin(t) = 0.55 with
       psi = atan(0.9 / 0.45), the eye line above: t = 12.85 degrees. s and c
       are its sine and cosine, times the grating's length. It is long and
       narrow, 0.7 by 2.2 m, because the order's colour runs ALONG it: a
       square one shows green and its neighbours, and the whole point is the
       end of the band that is not there. */
    const float gs = 0.7f, gl = 2.2f, s = 0.22240f * gl, c = 0.97496f * gl;

    scene = (HoloScene){
        .rects = {
            /* The back wall and the side wall: vertical, matte, white. */
            { .corner = { -3.0f, 0.0f, -2.0f },
              .edge_u = { 6.0f, 0.0f, 0.0f },
              .edge_v = { 0.0f, 3.0f, 0.0f },
              .albedo = { 0.92f, 0.92f, 0.92f } },
            { .corner = { -3.0f, 0.0f, -2.0f },
              .edge_u = { 0.0f, 0.0f, 5.0f },
              .edge_v = { 0.0f, 3.0f, 0.0f },
              .albedo = { 0.92f, 0.92f, 0.92f } },
            /* The slab, a metre up: what is under it has no sun. */
            { .corner = { 1.1f, 1.0f, -1.6f },
              .edge_u = { 1.6f, 0.0f, 0.0f },
              .edge_v = { 0.0f, 0.0f, 1.6f },
              .albedo = { 0.75f, 0.78f, 0.82f } },
            /* The grating: grooves along edge_u, its upper face leaning at
               the camera by `lean`, centred on (0, 1.1, 0). */
            { .corner = { -0.5f * gs, 1.1f + 0.5f * s, -0.5f * c },
              .edge_u = { gs, 0.0f, 0.0f },
              .edge_v = { 0.0f, -s, c },
              .albedo = { 1.0f, 1.0f, 1.0f },
              .grating_period = 1.0f, .grating_angle = 0.0f,
              .order_w = { 0.36f, 0.04f, 0.36f, 0.0f } },
        },
        .rect_count = 4,
        .spheres = {
            { .center = { -1.6f, 0.5f, -0.4f }, .radius = 0.5f,
              .albedo = { 0.95f, 0.95f, 0.95f } },
        },
        .sphere_count = 1,
        .has_floor = 1,
        .floor_y = 0.0f,
        .floor_a = { 0.80f, 0.80f, 0.82f },
        .floor_b = { 0.66f, 0.66f, 0.70f },
        /* The lamp: straight overhead, a 2 degree disk. */
        .sun_dir = { 0.0f, 1.0f, 0.0f },
        .sun_disk_cos = 0.99939f,
        .sun_disk_intensity = 32.0f,
        /* The sky is a lit ceiling, brighter overhead than toward the
           walls, so that both terms of the dome integral are in play. */
        .horizon = { 0.30f, 0.22f, 0.05f },
        .zenith  = { 0.95f, 0.72f, 0.16f },
        /* The sun and the sky ADD under sky_light, so between them they have
           to leave a white floor short of 1: the colour carries the lamp's
           strength as well as its hue. */
        .sun_color = { 0.62f, 0.50f, 0.0f },
        .sky_light = 0.55f,
    };
    if (white_mode) {
        scene.sun_color = hv3(0.58f, 0.58f, 0.58f);
        scene.horizon = hv3(0.25f, 0.25f, 0.27f);
        scene.zenith = hv3(0.85f, 0.86f, 0.90f);
    }

    HoloCamera cam = holo_camera_make(CAM_POS, CAM_AT, hv3(0, 1, 0), FOV, 1.0f);
    holo_gpu_scene_fill(&gpu, &scene, &cam, spectral_on);

    if (!holo_load_shader(shader_src, (int)sizeof shader_src)) {
        exit(2);
    }

    return holo_display_app(&(HoloDisplayDesc){
        .title = "hologram lamp",
        .fs_source = shader_src,
        .uniforms = &gpu,
        .uniforms_size = sizeof gpu,
        .after_frame = after_frame,
    });
}
