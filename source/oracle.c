/* See oracle.h. Talks to the display for the framebuffer size and pixels,
 * so it lives with the sokol-linked sources, not the pure ones. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "../external/sokol/sokol_app.h"
#include "display.h"
#include "oracle.h"
#include "scene_json.h"

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

/* The clamp, sRGB encode and UNORM rounding that the shader's one display
   boundary applies. Shared, so the comparison below and the reference written
   by holo_oracle_dump() can never encode a pixel differently. */
/* Two blobs into one file. The only writer in the dump path, so there is
   one place that can fail and one place that closes the handle. */
static int write_file(const char *path, const void *a, size_t a_size,
                      const void *b, size_t b_size) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        return 0;
    }
    if (a_size) {
        fwrite(a, 1, a_size, f);
    }
    if (b_size) {
        fwrite(b, 1, b_size, f);
    }
    fclose(f);
    return 1;
}

static int encode_u8(float v) {
    /* NaN-proof clamp: a NaN slipping out of a tracer must read as a wrong
       pixel, not poison the whole statistic. */
    v = v > 0 ? (v > 1 ? 1 : v) : 0;
    v = v <= 0.0031308f ? v * 12.92f
                        : 1.055f * powf(v, 1.0f / 2.4f) - 0.055f;
    return (int)(v * 255.0f + 0.5f);
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
            int want = encode_u8(rgb[3 * i + c]);
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

int holo_oracle_dump(const HoloScene *scene, const HoloCamera *cam,
                     int spectral, const void *gpu_scene, int gpu_scene_size,
                     const char *name) {
    /* The framebuffer the display is actually presenting, so the reference
       is the size the GPU tracer will be asked to match. */
    const int w = sapp_width(), h = sapp_height();
    char path[256];

    snprintf(path, sizeof path, "build/%s_params.bin", name);
    if (!write_file(path, gpu_scene, (size_t)gpu_scene_size, 0, 0)) {
        return 0;
    }

    float *rgb = malloc((size_t)w * h * 3 * sizeof(float));
    unsigned char *enc = malloc((size_t)w * h * 3);
    if (!rgb || !enc) {
        free(rgb);
        free(enc);
        return 0;
    }
    if (spectral) {
        holo_trace_image_spectral(scene, cam, w, h, rgb);
    } else {
        holo_trace_image(scene, cam, w, h, rgb);
    }
    for (int i = 0; i < w * h * 3; i++) {
        enc[i] = (unsigned char)encode_u8(rgb[i]);
    }

    /* Dimensions first, so the reader need not be told the frame size. */
    int dims[2] = { w, h };
    snprintf(path, sizeof path, "build/%s_ref.bin", name);
    int ok = write_file(path, dims, sizeof dims, enc, (size_t)w * h * 3);
    free(rgb);
    free(enc);
    if (!ok) {
        return 0;
    }

    /* The same scene as data. params.bin is the packed block a shader
       consumes -- correct, and unreadable to anyone who is not the
       shader. This is what the editor opens. It is a convenience, not
       part of the diff, so failing to write it does not fail the dump. */
    snprintf(path, sizeof path, "build/%s_scene.json", name);
    int json_ok = holo_scene_write_json(path, scene, cam, spectral);

    printf("dumped build/%s_params.bin (%d bytes) and build/%s_ref.bin (%dx%d)\n",
           name, gpu_scene_size, name, w, h);
    if (json_ok) {
        printf("dumped build/%s_scene.json\n", name);
    } else {
        printf("warning: could not write build/%s_scene.json\n", name);
    }
    return 1;
}
