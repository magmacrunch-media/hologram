/* See cpu_trace.h. */
#include <math.h>
#include "cpu_trace.h"
#include "spectrum.h"

/* Everything shading needs to know about the nearest surface: where it is,
   which way it faces, what it is made of, and whether its glass is a volume
   (spheres) or a thin pane (rects). Shading never asks WHICH surface. */
typedef struct {
    HoloV3 albedo;
    float  mirror;
    float  transmit;
    float  ior;
    float  disperse;
    int    volume;
} HoloSurface;

static int nearest_hit(const HoloScene *scene, HoloRay ray, HoloHit *hit,
                       HoloSurface *surf) {
    int found = 0;
    HoloHit best = { .t = 1e30f };
    HoloHit h;

    for (int i = 0; i < scene->sphere_count; i++) {
        if (holo_ray_sphere(ray, scene->spheres[i].center,
                            scene->spheres[i].radius, &h) && h.t < best.t) {
            best = h;
            surf->albedo = scene->spheres[i].albedo;
            surf->mirror = scene->spheres[i].mirror;
            surf->transmit = scene->spheres[i].transmit;
            surf->ior = scene->spheres[i].ior;
            surf->disperse = scene->spheres[i].disperse;
            surf->volume = 1;
            found = 1;
        }
    }
    for (int i = 0; i < scene->rect_count; i++) {
        if (holo_ray_rect(ray, scene->rects[i].corner, scene->rects[i].edge_u,
                          scene->rects[i].edge_v, &h) && h.t < best.t) {
            best = h;
            surf->albedo = scene->rects[i].albedo;
            surf->mirror = scene->rects[i].mirror;
            surf->transmit = scene->rects[i].transmit;
            surf->ior = scene->rects[i].ior;
            surf->disperse = scene->rects[i].disperse;
            surf->volume = 0;
            found = 1;
        }
    }
    if (scene->has_floor &&
        holo_ray_plane(ray, hv3(0, scene->floor_y, 0), hv3(0, 1, 0), &h) &&
        h.t < best.t) {
        best = h;
        /* 1m checker by cell parity. floorf, not a cast: casting truncates
           toward zero and would double the two cells either side of each
           axis. */
        int cell = (int)floorf(h.point.x) + (int)floorf(h.point.z);
        surf->albedo = (cell & 1) ? scene->floor_b : scene->floor_a;
        surf->mirror = scene->floor_mirror;
        surf->transmit = 0.0f;
        surf->ior = 1.0f;
        surf->disperse = 0.0f;
        surf->volume = 0;
        found = 1;
    }
    if (found) {
        *hit = best;
    }
    return found;
}

static int sun_blocked(const HoloScene *scene, HoloV3 point) {
    HoloRay shadow = { .origin = point, .dir = scene->sun_dir };
    HoloHit h;
    /* Mostly-clear glass does not throw a hard shadow -- without caustics
       (M8's problem) a black disc under a glass sphere would be more wrong
       than no shadow at all. */
    for (int i = 0; i < scene->sphere_count; i++) {
        if (scene->spheres[i].transmit <= 0.5f &&
            holo_ray_sphere(shadow, scene->spheres[i].center,
                            scene->spheres[i].radius, &h)) {
            return 1;
        }
    }
    for (int i = 0; i < scene->rect_count; i++) {
        if (scene->rects[i].transmit <= 0.5f &&
            holo_ray_rect(shadow, scene->rects[i].corner,
                          scene->rects[i].edge_u, scene->rects[i].edge_v, &h)) {
            return 1;
        }
    }
    /* The floor cannot shade anything: it is below everything and the sun is
       above it by convention. */
    return 0;
}

static HoloV3 sky(const HoloScene *scene, HoloV3 dir) {
    return hv3_lerp(scene->horizon, scene->zenith, 0.5f * (dir.y + 1.0f));
}

static float max3(HoloV3 v) {
    float m = v.x > v.y ? v.x : v.y;
    return m > v.z ? m : v.z;
}

/* A ray still owed to the image: where it is going, how much of the pixel's
   light rides on it, whether it is currently inside glass. */
typedef struct {
    HoloRay ray;
    HoloV3  tp;
    int     inside;
    int     depth;
} PathRay;

HoloV3 holo_trace_ray(const HoloScene *scene, HoloRay primary) {
    /* Glass forks light, mirrors merely redirect it, so the walk is a small
       LIFO stack of pending rays. Determinism is the contract: fixed caps,
       a fixed push order (refraction below reflection, so reflections are
       walked first), and a fixed cull threshold, all mirrored exactly in
       trace.hlsl -- the two sides must drop the same branches. */
    PathRay stack[HOLO_STACK];
    int sp = 0, processed = 0;
    HoloV3 color = hv3(0, 0, 0);

    stack[sp++] = (PathRay){ .ray = primary, .tp = hv3(1, 1, 1) };

    while (sp > 0 && processed < HOLO_MAX_RAYS) {
        processed++;
        PathRay p = stack[--sp];

        HoloHit hit;
        HoloSurface surf;
        if (!nearest_hit(scene, p.ray, &hit, &surf)) {
            color = hv3_add(color, hv3_mul(p.tp, sky(scene, p.ray.dir)));
            continue;
        }

        /* The matte share: Lambert under one sun, floored by the ambient
           stand-in -- full sun gives exactly the albedo, full shadow exactly
           HOLO_AMBIENT of it. */
        float matte = 1.0f - surf.mirror - surf.transmit;
        if (matte > 0.0f) {
            float diffuse = hv3_dot(hit.normal, scene->sun_dir);
            if (diffuse < 0.0f) {
                diffuse = 0.0f;
            }
            if (diffuse > 0.0f && sun_blocked(scene, hit.point)) {
                diffuse = 0.0f;
            }
            HoloV3 lambert = hv3_scale(surf.albedo,
                                       HOLO_AMBIENT + (1.0f - HOLO_AMBIENT) * diffuse);
            color = hv3_add(color, hv3_mul(p.tp, hv3_scale(lambert, matte)));
        }

        if (p.depth >= HOLO_MAX_BOUNCE) {
            continue;
        }

        /* The glass share splits by Fresnel: an untinted reflection and a
           refraction tinted by the glass's color. The metallic mirror share
           reflects along the same direction, tinted by albedo, so the two
           reflections travel as one ray. */
        HoloV3 reflect_tint = hv3_scale(surf.albedo, surf.mirror);
        if (surf.transmit > 0.0f) {
            float cos_i = -hv3_dot(hit.normal, p.ray.dir);
            float n1 = p.inside ? surf.ior : 1.0f;
            float n2 = p.inside ? 1.0f : surf.ior;
            float rs, rp;
            holo_fresnel(cos_i, n1, n2, &rs, &rp);
            float r = 0.5f * (rs + rp);

            if (r < 1.0f) {
                HoloV3 tp = hv3_mul(p.tp, hv3_scale(surf.albedo,
                                                    surf.transmit * (1.0f - r)));
                if (max3(tp) > HOLO_MIN_TP && sp < HOLO_STACK) {
                    PathRay t = { .tp = tp, .depth = p.depth + 1 };
                    if (surf.volume) {
                        /* In or out through the surface; the ray changes
                           medium. r < 1 guarantees Snell has an answer. */
                        hv3_refract(p.ray.dir, hit.normal, n1 / n2, &t.ray.dir);
                        t.ray.origin = hit.point;
                        t.inside = !p.inside;
                    } else {
                        /* A thin pane: one Fresnel interface, no net bend,
                           same medium on both sides. */
                        t.ray = (HoloRay){ .origin = hit.point, .dir = p.ray.dir };
                        t.inside = p.inside;
                    }
                    stack[sp++] = t;
                }
            }
            reflect_tint = hv3_add(reflect_tint,
                                   hv3_scale(hv3(1, 1, 1), surf.transmit * r));
        }

        HoloV3 tp = hv3_mul(p.tp, reflect_tint);
        if (max3(tp) > HOLO_MIN_TP && sp < HOLO_STACK) {
            stack[sp++] = (PathRay){
                .ray = { .origin = hit.point,
                         .dir = hv3_reflect(p.ray.dir, hit.normal) },
                .tp = tp,
                .inside = p.inside,
                .depth = p.depth + 1,
            };
        }
    }
    return color;
}

float holo_trace_lambda(const HoloScene *scene, HoloRay primary,
                        float lambda_um) {
    /* The same walk as holo_trace_ray -- same caps, same push order, same
       cull -- with a scalar throughput: one wavelength's worth of light.
       Albedos are read at lambda, and glass refracts at n(lambda), which is
       the entire point: two wavelengths handed the same primary ray part
       ways at the first dispersive surface. */
    PathRay stack[HOLO_STACK];
    int sp = 0, processed = 0;
    float intensity = 0.0f;

    stack[sp++] = (PathRay){ .ray = primary, .tp = hv3(1, 0, 0) };

    while (sp > 0 && processed < HOLO_MAX_RAYS) {
        processed++;
        PathRay p = stack[--sp];
        float tp = p.tp.x;   /* scalar throughput rides in x */

        HoloHit hit;
        HoloSurface surf;
        if (!nearest_hit(scene, p.ray, &hit, &surf)) {
            intensity += tp * holo_albedo_at(sky(scene, p.ray.dir), lambda_um);
            continue;
        }

        float matte = 1.0f - surf.mirror - surf.transmit;
        if (matte > 0.0f) {
            float diffuse = hv3_dot(hit.normal, scene->sun_dir);
            if (diffuse < 0.0f) {
                diffuse = 0.0f;
            }
            if (diffuse > 0.0f && sun_blocked(scene, hit.point)) {
                diffuse = 0.0f;
            }
            float lambert = holo_albedo_at(surf.albedo, lambda_um)
                          * (HOLO_AMBIENT + (1.0f - HOLO_AMBIENT) * diffuse);
            intensity += tp * lambert * matte;
        }

        if (p.depth >= HOLO_MAX_BOUNCE) {
            continue;
        }

        float reflect_tint = holo_albedo_at(surf.albedo, lambda_um) * surf.mirror;
        if (surf.transmit > 0.0f) {
            float n_glass = holo_ior_at(surf.ior, surf.disperse, lambda_um);
            float cos_i = -hv3_dot(hit.normal, p.ray.dir);
            float n1 = p.inside ? n_glass : 1.0f;
            float n2 = p.inside ? 1.0f : n_glass;
            float rs, rp;
            holo_fresnel(cos_i, n1, n2, &rs, &rp);
            float r = 0.5f * (rs + rp);

            if (r < 1.0f) {
                float ttp = tp * holo_albedo_at(surf.albedo, lambda_um)
                          * surf.transmit * (1.0f - r);
                if (ttp > HOLO_MIN_TP && sp < HOLO_STACK) {
                    PathRay t = { .tp = hv3(ttp, 0, 0), .depth = p.depth + 1 };
                    if (surf.volume) {
                        hv3_refract(p.ray.dir, hit.normal, n1 / n2, &t.ray.dir);
                        t.ray.origin = hit.point;
                        t.inside = !p.inside;
                    } else {
                        t.ray = (HoloRay){ .origin = hit.point, .dir = p.ray.dir };
                        t.inside = p.inside;
                    }
                    stack[sp++] = t;
                }
            }
            reflect_tint += surf.transmit * r;
        }

        float rtp = tp * reflect_tint;
        if (rtp > HOLO_MIN_TP && sp < HOLO_STACK) {
            stack[sp++] = (PathRay){
                .ray = { .origin = hit.point,
                         .dir = hv3_reflect(p.ray.dir, hit.normal) },
                .tp = hv3(rtp, 0, 0),
                .inside = p.inside,
                .depth = p.depth + 1,
            };
        }
    }
    return intensity;
}

HoloV3 holo_trace_ray_spectral(const HoloScene *scene, HoloRay ray) {
    HoloV3 color = hv3(0, 0, 0);
    for (int i = 0; i < HOLO_WAVELENGTHS; i++) {
        float in = holo_trace_lambda(scene, ray, holo_lambda(i));
        color = hv3_add(color, hv3_scale(holo_spectral_weight(i), in));
    }
    return color;
}

void holo_trace_image(const HoloScene *scene, const HoloCamera *cam,
                      int w, int h, float *rgb) {
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            HoloRay ray = holo_camera_ray(cam, (x + 0.5f) / (float)w,
                                               (y + 0.5f) / (float)h);
            HoloV3 c = holo_trace_ray(scene, ray);
            float *px = rgb + 3 * (y * w + x);
            px[0] = c.x;
            px[1] = c.y;
            px[2] = c.z;
        }
    }
}

void holo_trace_image_spectral(const HoloScene *scene, const HoloCamera *cam,
                               int w, int h, float *rgb) {
    /* The weights do not depend on the pixel; fetch them once. */
    HoloV3 weights[HOLO_WAVELENGTHS];
    for (int i = 0; i < HOLO_WAVELENGTHS; i++) {
        weights[i] = holo_spectral_weight(i);
    }
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            HoloRay ray = holo_camera_ray(cam, (x + 0.5f) / (float)w,
                                               (y + 0.5f) / (float)h);
            HoloV3 c = hv3(0, 0, 0);
            for (int i = 0; i < HOLO_WAVELENGTHS; i++) {
                float in = holo_trace_lambda(scene, ray, holo_lambda(i));
                c = hv3_add(c, hv3_scale(weights[i], in));
            }
            float *px = rgb + 3 * (y * w + x);
            px[0] = c.x;
            px[1] = c.y;
            px[2] = c.z;
        }
    }
}
