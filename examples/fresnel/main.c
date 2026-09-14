/* fresnel -- the lens examples/lens said it could not draw.
 *
 * Not a milestone; those are m0 through m9. This is the frame that exists
 * because the rings arrived: HoloFresnel, a lens of concentric prism rings
 * as one primitive, each ring's tilt computed from its radius when a ray
 * meets it rather than stored. The lens example's header ends by saying a
 * first-order Fresnel would need a primitive that did not exist yet, and
 * with it a cap raise. It needed the primitive. It did not need the cap
 * raise: forty rings cost five uniform slots, not forty.
 *
 * WHAT IS ON SCREEN. One Fresnel lens over a plain floor, aimed at a sun
 * ten degrees up, seen from its focus: the solid lens of examples/lens,
 * same glass, same rim, same focal length, cut into thirteen rings 4 cm
 * wide in a 5 cm slab where the solid one is 26 cm through.
 *
 * TEN DEGREES UP, because the floor is infinite. A ring's far edge leaves
 * 1.7 degrees off the axis away from it -- see TWO NUMBERS -- so in the
 * lower half of a lens aimed at the horizon every such ray tilts down, and
 * a ray with any downward component lands on the floor before it can see a
 * sun at the horizon. The first draft of this frame aimed at the horizon
 * and filled top-half only. A lighthouse solves it the same way: the beam
 * is thrown above the horizon, and here the whole cone, 10 +- 1.7 degrees,
 * clears the ground.
 *
 * FROM THE FOCUS THE APERTURE FILLS WITH SUN, and that is the whole frame --
 * the argument m8_furnace makes for a paraboloid, with Snell twice. Every
 * ring is cut so a ray from the focus meeting its peak leaves exactly down
 * the axis, and the ray meeting the rest of the ring leaves within the
 * ring's own angular width of it, which is what sets the two numbers below.
 * The rings show as thin dark circles: those are the risers, the vertical
 * steps between one ring's valley and the next ring's peak, seen nearly
 * edge-on and reflecting sky rather than passing sun. A lighthouse lens
 * shows the same lines from its lamp, and they are why it looks like a
 * beehive.
 *
 * The camera stands ON THE AXIS, which examples/lens's camera does not: its
 * two lenses share a focal plane and the camera sits between their axes,
 * 0.58 m off each, so from there every ray leaves them 23 degrees off and
 * the discs show the room inverted rather than the sun. A lens fills only
 * from its own focus, and a frame has one camera, so this one has one lens.
 *
 * Run with --spectral and the rings fringe on their own: each is its own
 * prism, and n runs with wavelength.
 *
 * TWO NUMBERS. The sun disk is two degrees wide (cos 0.9994), the same as
 * m8_furnace's, and the pitch is 4 cm, because the two are one decision. A
 * ring's tilt is exact at its peak radius r_k and the deviation a point at
 * r_k + pitch needs differs by pitch * f / (r^2 + f^2), 0.030 rad at the
 * centre for f = 1.34 -- inside a disk of half-angle 0.035 and outside the
 * half-degree disk the lens example uses. Narrower rings would fill under
 * a smaller sun; at 640x480 they would also be two pixels wide.
 *
 * ONE LENS PER FRAME. The uniform block had nine float4 slots left under
 * WebGL2's guaranteed 224 and a Fresnel is five of them, so the GPU draws
 * one. A first-order and a fourth-order lighthouse lens are traced one per
 * run, on the CPU, by the bake that reads the beam off them, and that tool
 * does not need a window at all.
 *
 *   build\fresnel.exe              look at it
 *   build\fresnel.exe --spectral   the same frame with wavelengths
 *   build\fresnel.exe --diff       hold it to the CPU oracle
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
static int spectral;
static int frames_drawn;

/* At the focus, on the axis, looking back through the lens. 1.34 m is the
   measured focus of examples/lens's solid lens (tests/test_trace.c scans
   for it) and the focal length this one is CUT for, so the two lenses are
   the same lens in two constructions.

   The axis is the sun's direction reversed -- the lens is aimed at the sun
   -- so the focus, center + FOCAL * axis, sits below and behind the lens,
   and the camera stands there looking at the lens's center. sin and cos of
   ten degrees, written out. */
static const float FOCAL = 1.34f;
static const HoloV3 LENS_CENTER = { 0.0f, 1.20f, 0.0f };
static const HoloV3 LENS_AXIS = { 0.0f, -0.17365f, 0.98481f };
static const HoloV3 SUN_DIR = { 0.0f, 0.17365f, -0.98481f };
static const HoloV3 CAM_POS = { 0.0f, 1.20f - 1.34f * 0.17365f, 1.34f * 0.98481f };
static const HoloV3 CAM_AT = { 0.0f, 1.20f, 0.0f };

static void after_frame(void) {
    frames_drawn++;
    if ((diff_mode || dump_mode) && frames_drawn == 5) {
        HoloCamera cam = holo_camera_make(CAM_POS, CAM_AT, hv3(0, 1, 0), 80.0f,
                                          (float)sapp_width() / (float)sapp_height());
        HoloOracleStats st;
        if (dump_mode) {
            exit(holo_oracle_dump(&scene, &cam, spectral, &gpu, sizeof gpu, "fresnel") ? 0 : 1);
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
        /* Grooves toward +z, so the focus is center + FOCAL * axis: the
           camera. The steepest ring, at the rim, tilts 28.5 degrees and
           cuts a 22 mm groove into the 50 mm slab. n = 1.58 at the D line
           with BK7's dispersion, as examples/lens quotes its glass. */
        .fresnels = { {
            .center = LENS_CENTER,
            .axis = LENS_AXIS,
            .focal = FOCAL,
            .r0 = 0.0f,
            .pitch = 0.04f,
            .rim = 0.50f,
            .thick = 0.05f,
            .albedo = { 1.0f, 1.0f, 1.0f },
            .transmit = 1.0f,
            .ior = 1.58f,
            .disperse = 0.0042f,
        } },
        .fresnel_count = 1,
        .has_floor = 1,
        .floor_y = 0.0f,
        .floor_a = { 0.80f, 0.79f, 0.77f },
        .floor_b = { 0.72f, 0.71f, 0.69f },
        .sun_dir = SUN_DIR,
        /* Two degrees, as m8_furnace: see TWO NUMBERS in the header. */
        .sun_disk_cos = 0.9994f,
        .sun_disk_intensity = 30.0f,
        .horizon = { 0.62f, 0.66f, 0.72f },
        .zenith = { 0.20f, 0.34f, 0.62f },
    };

    HoloCamera cam = holo_camera_make(CAM_POS, CAM_AT, hv3(0, 1, 0), 80.0f, 1.0f);
    holo_gpu_scene_fill(&gpu, &scene, &cam, spectral);

    if (!holo_load_shader(shader_src, (int)sizeof shader_src)) {
        exit(2);
    }

    return holo_display_app(&(HoloDisplayDesc){
        .title = "hologram fresnel",
        .fs_source = shader_src,
        .uniforms = &gpu,
        .uniforms_size = sizeof gpu,
        .after_frame = after_frame,
    });
}
