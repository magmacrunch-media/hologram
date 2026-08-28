/* See polar.h. Pure arithmetic; the tests hold it to the textbook
 * observables. */
#include <math.h>
#include "polar.h"

HoloSRow holo_srow_start(void) {
    return (HoloSRow){ 1, 0, 0, 0 };
}

HoloSRow holo_srow_rotate(HoloSRow s, float c2, float s2) {
    /* Row times R(theta) = [1,0,0,0; 0,c2,s2,0; 0,-s2,c2,0; 0,0,0,1]. */
    return (HoloSRow){ s.i,
                       c2 * s.q - s2 * s.u,
                       s2 * s.q + c2 * s.u,
                       s.v };
}

HoloSRow holo_srow_mueller(HoloSRow s, float a, float b, float c, float d) {
    return (HoloSRow){ a * s.i + b * s.q,
                       b * s.i + a * s.q,
                       c * s.u - d * s.v,
                       d * s.u + c * s.v };
}

HoloSRow holo_srow_polarizer(HoloSRow s) {
    float half = 0.5f * (s.i + s.q);
    return (HoloSRow){ half, half, 0, 0 };
}

HoloSRow holo_srow_scale(HoloSRow s, float k) {
    return (HoloSRow){ s.i * k, s.q * k, s.u * k, s.v * k };
}

void holo_frame_rot(HoloV3 frame, HoloV3 target, HoloV3 dir,
                    float *c2, float *s2) {
    /* cos and sin of the single angle from two dot products, then the
       double angle by identity -- no atan, and exact when the inputs are
       unit and transverse. */
    float c = hv3_dot(frame, target);
    float s = hv3_dot(hv3_cross(frame, target), dir);
    *c2 = c * c - s * s;
    *s2 = 2.0f * c * s;
}

int holo_fresnel_amp(float cos_i, float n1, float n2,
                     float *rs, float *rp, float *ts, float *tp,
                     float *f, float *delta) {
    float sin_i2 = 1.0f - cos_i * cos_i;
    float sin_t = (n1 / n2) * sqrtf(sin_i2);
    if (sin_t >= 1.0f) {
        /* TIR: unit reflection with a phase difference between p and s --
           tan(delta_s/2) = g / cos_i, tan(delta_p/2) = g / (n^2 cos_i)
           with n = n2/n1 < 1 and g = sqrt(sin^2 - n^2). */
        float n = n2 / n1;
        float g = sqrtf(sin_i2 - n * n);
        *rs = 1.0f;
        *rp = 1.0f;
        *ts = 0.0f;
        *tp = 0.0f;
        *f = 0.0f;
        *delta = 2.0f * (atanf(g / (n * n * cos_i)) - atanf(g / cos_i));
        return 1;
    }
    float cos_t = sqrtf(1.0f - sin_t * sin_t);
    *rs = (n1 * cos_i - n2 * cos_t) / (n1 * cos_i + n2 * cos_t);
    *rp = (n1 * cos_t - n2 * cos_i) / (n1 * cos_t + n2 * cos_i);
    *ts = 2.0f * n1 * cos_i / (n1 * cos_i + n2 * cos_t);
    *tp = 2.0f * n1 * cos_i / (n1 * cos_t + n2 * cos_i);
    *f = (n2 * cos_t) / (n1 * cos_i);
    *delta = 0.0f;
    return 0;
}
