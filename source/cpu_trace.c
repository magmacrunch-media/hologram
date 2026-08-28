/* See cpu_trace.h. */
#include <math.h>
#include "cpu_trace.h"

/* Nearest surface the ray meets. Returns the sphere index, HIT_FLOOR, or
   HIT_NOTHING; fills *hit when it hits anything. */
#define HIT_NOTHING (-1)
#define HIT_FLOOR   (-2)

static int nearest_hit(const HoloScene *scene, HoloRay ray, HoloHit *hit) {
    int what = HIT_NOTHING;
    HoloHit best = { .t = 1e30f };
    HoloHit h;
    for (int i = 0; i < scene->sphere_count; i++) {
        if (holo_ray_sphere(ray, scene->spheres[i].center,
                            scene->spheres[i].radius, &h) && h.t < best.t) {
            best = h;
            what = i;
        }
    }
    if (scene->has_floor &&
        holo_ray_plane(ray, hv3(0, scene->floor_y, 0), hv3(0, 1, 0), &h) &&
        h.t < best.t) {
        best = h;
        what = HIT_FLOOR;
    }
    if (what != HIT_NOTHING) {
        *hit = best;
    }
    return what;
}

static int sun_blocked(const HoloScene *scene, HoloV3 point) {
    HoloRay shadow = { .origin = point, .dir = scene->sun_dir };
    HoloHit h;
    for (int i = 0; i < scene->sphere_count; i++) {
        if (holo_ray_sphere(shadow, scene->spheres[i].center,
                            scene->spheres[i].radius, &h)) {
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

HoloV3 holo_trace_ray(const HoloScene *scene, HoloRay ray) {
    HoloHit hit;
    int what = nearest_hit(scene, ray, &hit);
    if (what == HIT_NOTHING) {
        return sky(scene, ray.dir);
    }

    HoloV3 albedo;
    if (what == HIT_FLOOR) {
        /* 1m checker by cell parity. floorf, not a cast: casting truncates
           toward zero and would double the two cells either side of each
           axis. */
        int cell = (int)floorf(hit.point.x) + (int)floorf(hit.point.z);
        albedo = (cell & 1) ? scene->floor_b : scene->floor_a;
    } else {
        albedo = scene->spheres[what].albedo;
    }

    float diffuse = hv3_dot(hit.normal, scene->sun_dir);
    if (diffuse < 0.0f) {
        diffuse = 0.0f;
    }
    if (sun_blocked(scene, hit.point)) {
        diffuse = 0.0f;
    }
    /* Lambert under one sun, floored by the ambient stand-in: full sun gives
       exactly the albedo, full shadow exactly HOLO_AMBIENT of it. */
    return hv3_scale(albedo, HOLO_AMBIENT + (1.0f - HOLO_AMBIENT) * diffuse);
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
