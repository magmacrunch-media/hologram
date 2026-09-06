/* lens -- white light through a single refracting element, and the colours
 * it cannot help separating.
 *
 * Not a milestone; those are m0 through m9. This is the frame that exists
 * because a dish learned to be glass, and because a lens is the one optical
 * object a rasterizer cannot fake convincingly. A game engine draws a
 * lighthouse's beam as a cone mesh with a glow sprite. What a lighthouse
 * actually has is a lens, and a lens does something a cone cannot: it bends
 * every wavelength by a different amount, because n is a function of lambda
 * and Snell's law does not care that the difference is inconvenient.
 *
 * WHAT IS ON SCREEN. Two lenses of the same shape, side by side over a plain
 * floor, with a low sun behind them:
 *
 *     left    BK7, ior 1.5168 at the D line, Cauchy B 0.0042
 *     right   dense flint, ior 1.62, B 0.025 -- six times the dispersion
 *
 * Same geometry, same light, same everything but the glass. The flint's
 * focus is smeared into a coloured line along the axis and the crown's is
 * nearly a point, which is the entire reason achromatic doublets exist and
 * the reason Newton gave up on refractors and built a mirror instead.
 *
 * Run with --spectral to see it. In the RGB path there is no wavelength, so
 * both lenses focus identically and the frame is a control rather than a
 * result: that is worth looking at once, because it is exactly what every
 * non-spectral renderer would show you and it is wrong in a way that looks
 * fine.
 *
 * HOW A LENS IS BUILT OUT OF DISHES. A dish is the surface z = r^2 / 2R in
 * its own frame, a bowl opening along its axis, so a biconvex lens is two of
 * them back to back:
 *
 *     front   apex at -t/2, axis +z   bulging toward the incoming light
 *     rear    apex at +t/2, axis -z   bulging away from it
 *
 * A ray entering the front refracts into the glass, the tracer's `inside`
 * flag flips, it crosses, and it refracts out at the rear. Two surfaces, two
 * applications of Snell, and the convergence falls out -- nothing here knows
 * what a focal length is.
 *
 * THIS IS NOT A FRESNEL LENS, and the difference matters if the beam from
 * Southeast Light is ever the goal. A first-order Fresnel is dozens of
 * concentric annular prisms, each with its own tilt, ground so that a short
 * stack of glass does the work of a lens metres thick. There are four dish
 * slots. This is the single refracting element that fits inside them, which
 * is the honest half of the problem: it shows the dispersion, and it does
 * not show the rings. The rings need an annular-prism primitive that does
 * not exist yet, and with it, a cap raise.
 *
 *   build\lens.exe              look at it
 *   build\lens.exe --spectral   the same frame with wavelengths, which is
 *                               the only way the two glasses differ
 *   build\lens.exe --diff       hold it to the CPU oracle
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
static int spectral;
static int frames_drawn;

/* Low and close to the axis, so the cones of converging light cross the
   frame lengthways rather than pointing at the camera. */
/* ON THE FOCAL PLANE, LOOKING BACK THROUGH THEM. That position is the whole
   frame: from the focus every ray through the lens leaves parallel to the
   axis and lands in the sun, so the aperture fills with light -- the same
   argument m8_furnace makes for a paraboloid, with Snell twice instead of one
   reflection. Anywhere else the lenses are two discs with the scene upside
   down in them, which is pretty and says nothing about wavelength.

   1.34 m is measured, not derived: tests/test_trace.c scans the axis for the
   brightest position and this geometry peaks there. Thin-lens optics would
   say 1.47, and the difference is the thickness -- which is why the number
   here comes from the tracer rather than from the formula. */
static const HoloV3 CAM_POS = { 0.0f, 1.20f, 1.34f };
static const HoloV3 CAM_AT = { 0.0f, 1.20f, -1.0f };

/* One lens, as two dishes. `x` is where it stands, `ior_d` and `cauchy_b`
   are its glass. Thickness and curvature are shared so the two lenses in the
   frame differ in nothing but the material. */
static void add_lens(HoloScene *s, float x, float ior_d, float cauchy_b) {
    const float thick = 0.26f, curv_r = 1.7f, rim = 0.50f;
    const float y = 1.20f;
    s->dishes[s->dish_count++] = (HoloDish){
        .apex = { x, y, -thick * 0.5f },
        .axis = { 0.0f, 0.0f, 1.0f },
        .curv_r = curv_r,
        .conic_k = 0.0f,
        .rim = rim,
        .albedo = { 1.0f, 1.0f, 1.0f },
        .transmit = 1.0f,
        .ior = ior_d,
        .disperse = cauchy_b,
    };
    s->dishes[s->dish_count++] = (HoloDish){
        .apex = { x, y, thick * 0.5f },
        .axis = { 0.0f, 0.0f, -1.0f },
        .curv_r = curv_r,
        .conic_k = 0.0f,
        .rim = rim,
        .albedo = { 1.0f, 1.0f, 1.0f },
        .transmit = 1.0f,
        .ior = ior_d,
        .disperse = cauchy_b,
    };
}

static void after_frame(void) {
    frames_drawn++;
    if ((diff_mode || dump_mode) && frames_drawn == 5) {
        HoloCamera cam = holo_camera_make(CAM_POS, CAM_AT, hv3(0, 1, 0), 80.0f,
                                          (float)sapp_width() / (float)sapp_height());
        HoloOracleStats st;
        if (dump_mode) {
            exit(holo_oracle_dump(&scene, &cam, spectral, &gpu, sizeof gpu, "lens") ? 0 : 1);
        }
        int ok = holo_oracle_diff(&scene, &cam, spectral, &st);
        printf("DIFF %s: %dx%d, mean err %.4f/255, max %d/255, "
               "%.3f%% pixels off by >8\n",
               ok ? "OK" : "FAIL", st.width, st.height, st.mean, st.max, st.outlier_pct);
        exit(ok ? 0 : 1);
    }
}

sapp_desc sokol_main(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--diff") == 0) diff_mode = 1;
        else if (strcmp(argv[i], "--dump") == 0) dump_mode = 1;
        else if (strcmp(argv[i], "--spectral") == 0) spectral = 1;
    }

    scene = (HoloScene){
        .has_floor = 1,
        .floor_y = 0.0f,
        /* Nearly white and nearly uniform. The floor is the screen the
           focused light lands on, and a loud checker would compete with the
           thing worth looking at. */
        .floor_a = { 0.80f, 0.79f, 0.77f },
        .floor_b = { 0.72f, 0.71f, 0.69f },
        /* Behind the lenses and low, so the light they gather crosses the
           frame toward the camera instead of into it. */
        .sun_dir = { 0.0f, 0.0f, -1.0f },
        /* A SMALL, BRIGHT DISK. The focus is only as sharp as the source is
           small -- a two-degree sun smears every focal point into a two-
           degree blur and the two glasses would look alike. This is nearer
           the sun's real half-degree, and it costs nothing. */
        .sun_disk_cos = 0.99996f,
        .sun_disk_intensity = 30.0f,
        .horizon = { 0.62f, 0.66f, 0.72f },
        .zenith = { 0.20f, 0.34f, 0.62f },
    };

    /* THE SAME INDEX, DIFFERENT DISPERSION, which is the whole design of the
       comparison and was wrong in the first version of this file. Quoting one
       lens as BK7 and the other as a dense flint changes n as well as B, so
       the two would have had different focal lengths and the frame would have
       shown that instead -- one lens sharp and one out of focus, which is a
       confound and not a result.
     *
       Both are n = 1.58 at the D line, so both focus in the same place. Only
       the Cauchy B differs, by a factor of seven. Everything that is left
       between them is dispersion. */
    add_lens(&scene, -0.58f, 1.58f, 0.0042f);
    add_lens(&scene, 0.58f, 1.58f, 0.030f);

    HoloCamera cam = holo_camera_make(CAM_POS, CAM_AT, hv3(0, 1, 0), 80.0f, 1.0f);
    holo_gpu_scene_fill(&gpu, &scene, &cam, spectral);

    if (!holo_load_shader(shader_src, (int)sizeof shader_src)) {
        exit(2);
    }

    return holo_display_app(&(HoloDisplayDesc){
        .title = "hologram lens",
        .fs_source = shader_src,
        .uniforms = &gpu,
        .uniforms_size = sizeof gpu,
        .after_frame = after_frame,
    });
}
