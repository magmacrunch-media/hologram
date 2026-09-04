/* Round-trip check for editor/core/emit.js: does the C the editor emits pack
 * to the same bytes as the C the scene came from?
 *
 * This is the whole path the editor exists to serve --
 *
 *     m7_room.exe --dump  ->  scene.json  ->  editor  ->  C  ->  a game
 *
 * -- and the claim it has to earn is that nothing changes on the way round.
 *
 * The literal below was copied verbatim out of the editor's "show C" panel,
 * having opened build/m7_room_scene.json and changed nothing. Compiling and
 * packing it must reproduce the block build\m7_room.exe --dump wrote, past
 * the display header in slot 0 that display.c fills every frame.
 *
 * Not part of build.bat test: it needs a dump, which needs a GPU and a window,
 * and the host tests deliberately need neither. Run it by hand after changing
 * emit.js, regenerating the literal from the panel first:
 *
 *     build.bat
 *     build\m7_room.exe --dump
 *     cl /nologo /std:c11 /W4 /Isource /Febuild\roundtrip.exe ^
 *         editor/roundtrip.c source/linalg.c source/polar.c source/geometry.c ^
 *         source/camera.c source/collision.c source/cpu_trace.c ^
 *         source/gpu_scene.c source/scene_json.c source/spectrum.c ^
 *         source/timestep.c
 *     build\roundtrip.exe
 *
 * Last run: 840 of 840 floats identical.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../source/gpu_scene.h"

int main(void) {
    HoloScene scene;
    HoloCamera cam;
    float aspect = 640.0f / 480.0f;

    /* ---- BEGIN emitted by the editor ---- */
    scene = (HoloScene){
        .spheres = {
            { .center = { 0.0f, 1.0f, -2.0f }, .radius = 1.0f,
              .albedo = { 1.0f, 1.0f, 1.0f }, .transmit = 1.0f, .ior = 1.62f,
              .disperse = 0.02f },
            { .center = { -2.2f, 0.6f, -4.0f }, .radius = 0.6f,
              .albedo = { 0.95f, 0.95f, 0.98f }, .mirror = 0.85f },
            { .center = { 2.3f, 0.5f, -3.2f }, .radius = 0.5f,
              .albedo = { 0.85f, 0.25f, 0.35f } },
        },
        .sphere_count = 3,
        .rects = {
            { .corner = { -4.0f, 0.0f, -6.0f }, .edge_u = { 0.0f, 0.0f, 12.0f },
              .edge_v = { 0.0f, 3.0f, 0.0f }, .albedo = { 0.88f, 0.92f, 0.95f },
              .mirror = 1.0f },
            { .corner = { 4.0f, 0.0f, -6.0f }, .edge_u = { 0.0f, 0.0f, 12.0f },
              .edge_v = { 0.0f, 3.0f, 0.0f }, .albedo = { 0.88f, 0.92f, 0.95f },
              .mirror = 1.0f },
            { .corner = { -4.0f, 0.0f, -6.0f }, .edge_u = { 3.0f, 0.0f, 0.0f },
              .edge_v = { 0.0f, 3.0f, 0.0f }, .albedo = { 0.75f, 0.7f, 0.62f } },
            { .corner = { 1.0f, 0.0f, -6.0f }, .edge_u = { 3.0f, 0.0f, 0.0f },
              .edge_v = { 0.0f, 3.0f, 0.0f }, .albedo = { 0.75f, 0.7f, 0.62f } },
            { .corner = { -1.0f, 0.0f, -6.0f }, .edge_u = { 2.0f, 0.0f, 0.0f },
              .edge_v = { 0.0f, 3.0f, 0.0f }, .albedo = { 1.0f, 1.0f, 1.0f },
              .transmit = 1.0f, .ior = 1.5f },
            { .corner = { -4.0f, 0.0f, 6.0f }, .edge_u = { 8.0f, 0.0f, 0.0f },
              .edge_v = { 0.0f, 3.0f, 0.0f }, .albedo = { 0.6f, 0.62f, 0.66f } },
            { .corner = { 0.8f, 0.0f, 0.6f }, .edge_u = { 2.0f, 0.0f, 0.0f },
              .edge_v = { 0.0f, 2.4f, 0.0f }, .albedo = { 1.0f, 1.0f, 1.0f },
              .filter = HOLO_POLARIZER },
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

    cam = holo_camera_make(hv3(0.0f, 1.55f, 4.5f),
                           hv3(-8.742278e-8f, 1.55f, 3.5f),
                           hv3(0, 1, 0), 70.00001f, aspect);
    /* ---- END emitted by the editor ---- */

    HoloGpuScene gpu;
    memset(&gpu, 0, sizeof gpu);
    holo_gpu_scene_fill(&gpu, &scene, &cam, 1);   /* m7_room dumps spectral */

    FILE *f = fopen("build/m7_room_params.bin", "rb");
    if (!f) {
        printf("cannot open build/m7_room_params.bin -- run build\\m7_room.exe --dump\n");
        return 2;
    }
    static unsigned char golden[sizeof(HoloGpuScene)];
    size_t n = fread(golden, 1, sizeof golden, f);
    fclose(f);
    if (n != sizeof golden) {
        printf("size mismatch: dump is %zu bytes, HoloGpuScene is %zu\n",
               n, sizeof golden);
        return 1;
    }

    const float *a = (const float *)golden;
    const float *b = (const float *)&gpu;
    const int count = (int)(sizeof golden / sizeof(float));

    /* Slot 0 is HoloDisplayUniforms, which display.c writes each frame. */
    int differ = 0, worst_i = -1;
    double worst = 0.0;
    for (int i = 4; i < count; i++) {
        if (a[i] == b[i]) {
            continue;
        }
        differ++;
        double d = fabs((double)a[i] - (double)b[i]);
        if (d > worst) { worst = d; worst_i = i; }
    }

    printf("round trip: %d of %d floats identical\n", count - 4 - differ, count - 4);
    if (differ) {
        printf("  %d differ, worst float %d (slot %d lane %d): "
               "dump %.9g vs emitted %.9g, |d| = %.3g\n",
               differ, worst_i, worst_i / 4, worst_i % 4,
               (double)a[worst_i], (double)b[worst_i], worst);
    }
    printf("%s\n", differ ? "EMITTER FAIL" : "EMITTER OK: byte-identical");
    return differ ? 1 : 0;
}
