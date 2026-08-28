/* See oracle.h. Talks to the display for the framebuffer size and pixels,
 * so it lives with the sokol-linked sources, not the pure ones. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "../external/sokol/sokol_app.h"
#include "display.h"
#include "oracle.h"

/* On a failed diff, both frames land in build\ as PPMs: when the verdict
   is "the twins disagree", the first question is always "where". */
static void dump_ppm(const char *path, const unsigned char *rgba,
                     const float *rgb, int w, int h) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        for (int c = 0; c < 3; c++) {
            if (rgba) {
                fputc(rgba[4 * i + c], f);
            } else {
                float v = rgb[3 * i + c];
                v = v < 0 ? 0 : v > 1 ? 1 : v;
                v = v <= 0.0031308f ? v * 12.92f
                                    : 1.055f * powf(v, 1.0f / 2.4f) - 0.055f;
                fputc((int)(v * 255.0f + 0.5f), f);
            }
        }
    }
    fclose(f);
}

int holo_oracle_diff(const HoloScene *scene, const HoloCamera *cam,
                     int spectral, HoloOracleStats *stats) {
    const int w = sapp_width(), h = sapp_height();
    stats->width = w;
    stats->height = h;
    stats->mean = -1.0;
    stats->max = 0;
    stats->outlier_pct = 100.0;

    unsigned char *gpu = malloc((size_t)w * h * 4);
    float *rgb = malloc((size_t)w * h * 3 * sizeof(float));
    if (!gpu || !rgb || !holo_display_read_frame(gpu, w, h)) {
        free(gpu);
        free(rgb);
        return 0;
    }
    if (spectral) {
        holo_trace_image_spectral(scene, cam, w, h, rgb);
    } else {
        holo_trace_image(scene, cam, w, h, rgb);
    }

    long long sum = 0;
    int outliers = 0, max_diff = 0;
    for (int i = 0; i < w * h; i++) {
        for (int c = 0; c < 3; c++) {
            float v = rgb[3 * i + c];
            /* NaN-proof clamp: a NaN slipping out of a tracer must read
               as a wrong pixel, not poison the whole statistic. */
            v = v > 0 ? (v > 1 ? 1 : v) : 0;
            /* The same sRGB encode the shader applies at its one display
               boundary, then the same rounding the UNORM store applies. */
            v = v <= 0.0031308f ? v * 12.92f
                                : 1.055f * powf(v, 1.0f / 2.4f) - 0.055f;
            int want = (int)(v * 255.0f + 0.5f);
            int got = gpu[4 * i + c];
            int d = got > want ? got - want : want - got;
            sum += d;
            if (d > max_diff) {
                max_diff = d;
            }
            if (d > 8) {
                outliers++;
                break;
            }
        }
    }
    stats->mean = (double)sum / ((double)w * h * 3);
    stats->max = max_diff;
    stats->outlier_pct = 100.0 * outliers / ((double)w * h);
    /* 0.75% outliers: silhouettes and near-critical-angle glass are razor
       edges where the two sides legitimately flip independent float coins,
       and a dispersive ball traced at twelve wavelengths has twelve TIR
       rims. The mean is the load-bearing bar. */
    int ok = stats->mean < 1.0 && stats->outlier_pct < 0.75;
    if (!ok) {
        dump_ppm("build\\diff_gpu.ppm", gpu, 0, w, h);
        dump_ppm("build\\diff_cpu.ppm", 0, rgb, w, h);
    }
    free(gpu);
    free(rgb);
    return ok;
}
