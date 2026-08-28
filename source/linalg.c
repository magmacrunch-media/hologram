/* See linalg.h. Everything here is arithmetic; the tests hold it to the
 * closed-form answers (Snell angles, mirror images, the lot). */
#include <math.h>
#include "linalg.h"

HoloV3 hv3(float x, float y, float z) { return (HoloV3){ x, y, z }; }

HoloV3 hv3_add(HoloV3 a, HoloV3 b)  { return hv3(a.x + b.x, a.y + b.y, a.z + b.z); }
HoloV3 hv3_sub(HoloV3 a, HoloV3 b)  { return hv3(a.x - b.x, a.y - b.y, a.z - b.z); }
HoloV3 hv3_mul(HoloV3 a, HoloV3 b)  { return hv3(a.x * b.x, a.y * b.y, a.z * b.z); }
HoloV3 hv3_scale(HoloV3 a, float s) { return hv3(a.x * s, a.y * s, a.z * s); }

float hv3_dot(HoloV3 a, HoloV3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

HoloV3 hv3_cross(HoloV3 a, HoloV3 b) {
    return hv3(a.y * b.z - a.z * b.y,
               a.z * b.x - a.x * b.z,
               a.x * b.y - a.y * b.x);
}

float hv3_len(HoloV3 a) { return sqrtf(hv3_dot(a, a)); }

HoloV3 hv3_norm(HoloV3 a) {
    float len = hv3_len(a);
    return len > 1e-8f ? hv3_scale(a, 1.0f / len) : a;
}

HoloV3 hv3_lerp(HoloV3 a, HoloV3 b, float t) {
    return hv3_add(a, hv3_scale(hv3_sub(b, a), t));
}

HoloV3 hv3_reflect(HoloV3 d, HoloV3 n) {
    return hv3_sub(d, hv3_scale(n, 2.0f * hv3_dot(d, n)));
}

void holo_fresnel(float cos_i, float n1, float n2, float *rs, float *rp) {
    float sin_i = sqrtf(1.0f - cos_i * cos_i);
    float sin_t = (n1 / n2) * sin_i;
    if (sin_t >= 1.0f) {
        *rs = 1.0f;   /* total internal reflection: the interface is a mirror */
        *rp = 1.0f;
        return;
    }
    float cos_t = sqrtf(1.0f - sin_t * sin_t);
    float rs_amp = (n1 * cos_i - n2 * cos_t) / (n1 * cos_i + n2 * cos_t);
    float rp_amp = (n1 * cos_t - n2 * cos_i) / (n1 * cos_t + n2 * cos_i);
    *rs = rs_amp * rs_amp;
    *rp = rp_amp * rp_amp;
}

int hv3_refract(HoloV3 d, HoloV3 n, float eta, HoloV3 *out) {
    /* k is cos^2 of the transmitted angle by Snell; when it goes negative the
       transmitted angle would need sin > 1, which is TIR by definition. */
    float cos_i = -hv3_dot(n, d);
    float k = 1.0f - eta * eta * (1.0f - cos_i * cos_i);
    if (k < 0.0f) {
        return 0;
    }
    *out = hv3_sub(hv3_scale(d, eta),
                   hv3_scale(n, eta * -cos_i + sqrtf(k)));
    return 1;
}
