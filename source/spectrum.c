/* See spectrum.h. */
#include <math.h>
#include "spectrum.h"

float holo_lambda(int i) {
    return 0.42f + (0.68f - 0.42f) * (float)i / (float)(HOLO_WAVELENGTHS - 1);
}

/* The Wyman-Sloan-Shirley piecewise-Gaussian fits to the CIE 1931 color
   matching functions (JCGT 2013). lambda in nanometers here, as published.
   Each lobe is a Gaussian with different widths left and right of center. */
static float lobe(float nm, float center, float sl, float sr, float amp) {
    float s = nm < center ? sl : sr;
    float t = (nm - center) / s;
    return amp * expf(-0.5f * t * t);
}

static float cmf_x(float nm) {
    return lobe(nm, 599.8f, 37.9f, 31.0f, 1.056f)
         + lobe(nm, 442.0f, 16.0f, 26.7f, 0.362f)
         + lobe(nm, 501.1f, 20.4f, 26.2f, -0.065f);
}

static float cmf_y(float nm) {
    return lobe(nm, 568.8f, 46.9f, 40.5f, 0.821f)
         + lobe(nm, 530.9f, 16.3f, 31.1f, 0.286f);
}

static float cmf_z(float nm) {
    return lobe(nm, 437.0f, 11.8f, 36.0f, 1.217f)
         + lobe(nm, 459.0f, 26.0f, 13.8f, 0.681f);
}

HoloV3 holo_spectral_weight(int i) {
    /* XYZ -> linear sRGB (D65), then a per-channel normalization so a flat
       spectrum sums to exact white. Computed fresh each call -- these are a
       handful of exps, and keeping the function pure keeps it testable.
       Callers that care (the render loop) fetch the weights once. */
    HoloV3 rgb_sum = hv3(0, 0, 0);
    HoloV3 rgb_i = hv3(0, 0, 0);
    for (int k = 0; k < HOLO_WAVELENGTHS; k++) {
        float nm = holo_lambda(k) * 1000.0f;
        float x = cmf_x(nm), y = cmf_y(nm), z = cmf_z(nm);
        HoloV3 rgb = hv3( 3.2406f * x - 1.5372f * y - 0.4986f * z,
                         -0.9689f * x + 1.8758f * y + 0.0415f * z,
                          0.0557f * x - 0.2040f * y + 1.0570f * z);
        rgb_sum = hv3_add(rgb_sum, rgb);
        if (k == i) {
            rgb_i = rgb;
        }
    }
    return hv3(rgb_i.x / rgb_sum.x, rgb_i.y / rgb_sum.y, rgb_i.z / rgb_sum.z);
}

float holo_albedo_at(HoloV3 rgb, float lambda_um) {
    /* Three smooth bands: blue below 0.475, green around 0.53, red above
       0.61, linear blends between. The bands partition unity, so a neutral
       color reads the same at every wavelength. */
    float t_bg = (lambda_um - 0.475f) / (0.510f - 0.475f);
    float t_gr = (lambda_um - 0.565f) / (0.610f - 0.565f);
    t_bg = t_bg < 0 ? 0 : t_bg > 1 ? 1 : t_bg;
    t_gr = t_gr < 0 ? 0 : t_gr > 1 ? 1 : t_gr;
    float b = 1.0f - t_bg;
    float r = t_gr;
    float g = t_bg - t_gr;
    return rgb.x * r + rgb.y * g + rgb.z * b;
}

float holo_ior_at(float ior_d, float cauchy_b, float lambda_um) {
    const float inv_d2 = 1.0f / (0.5893f * 0.5893f);
    return ior_d + cauchy_b * (1.0f / (lambda_um * lambda_um) - inv_d2);
}
