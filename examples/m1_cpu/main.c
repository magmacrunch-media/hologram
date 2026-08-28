/* M1: the CPU oracle's first picture. Three spheres on the checkered floor
 * under an afternoon sun, 640x480, written to build\m1_cpu.ppm -- rendered
 * by the exact code the tests hold to Snell and Lambert, which is the whole
 * point: from M2 on, the GPU is diffed against frames made this way.
 */
#include <stdio.h>
#include "../../hologram.h"

int main(void) {
    const int W = 640, H = 480;
    static float rgb[640 * 480 * 3];

    const HoloScene scene = {
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
        /* Sun leaning in from the upper left so the shadows say which way
           is which. Normalized by hand: (-3, 6, 2) / 7. */
        .sun_dir = { -3.0f / 7, 6.0f / 7, 2.0f / 7 },
        .horizon = { 1.0f, 0.9f, 0.8f },
        .zenith  = { 0.25f, 0.45f, 0.9f },
    };

    const HoloCamera cam = holo_camera_make(
        (HoloV3){ 0.0f, 1.6f, 6.0f }, (HoloV3){ 0.0f, 0.8f, 0.0f },
        (HoloV3){ 0.0f, 1.0f, 0.0f }, 55.0f, (float)W / (float)H);

    holo_trace_image(&scene, &cam, W, H, rgb);

    FILE *f = fopen("build\\m1_cpu.ppm", "wb");
    if (!f) {
        printf("could not open build\\m1_cpu.ppm -- run from the repo root\n");
        return 1;
    }
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H * 3; i++) {
        float v = rgb[i] < 0 ? 0 : rgb[i] > 1 ? 1 : rgb[i];
        fputc((int)(v * 255.0f + 0.5f), f);
    }
    fclose(f);
    printf("wrote build\\m1_cpu.ppm (%dx%d)\n", W, H);
    return 0;
}
