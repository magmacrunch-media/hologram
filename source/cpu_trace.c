/* See cpu_trace.h. */
#include <math.h>
#include "cpu_trace.h"

/* What the nearest surface along the ray is made of. Shading never asks
   WHICH surface answered -- only where it is, which way it faces, and what
   it is made of, so that is all this returns. */
static int nearest_hit(const HoloScene *scene, HoloRay ray, HoloHit *hit,
                       HoloV3 *albedo, float *mirror) {
    int found = 0;
    HoloHit best = { .t = 1e30f };
    HoloHit h;

    for (int i = 0; i < scene->sphere_count; i++) {
        if (holo_ray_sphere(ray, scene->spheres[i].center,
                            scene->spheres[i].radius, &h) && h.t < best.t) {
            best = h;
            *albedo = scene->spheres[i].albedo;
            *mirror = scene->spheres[i].mirror;
            found = 1;
        }
    }
    for (int i = 0; i < scene->rect_count; i++) {
        if (holo_ray_rect(ray, scene->rects[i].corner, scene->rects[i].edge_u,
                          scene->rects[i].edge_v, &h) && h.t < best.t) {
            best = h;
            *albedo = scene->rects[i].albedo;
            *mirror = scene->rects[i].mirror;
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
        *albedo = (cell & 1) ? scene->floor_b : scene->floor_a;
        *mirror = scene->floor_mirror;
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
    for (int i = 0; i < scene->sphere_count; i++) {
        if (holo_ray_sphere(shadow, scene->spheres[i].center,
                            scene->spheres[i].radius, &h)) {
            return 1;
        }
    }
    for (int i = 0; i < scene->rect_count; i++) {
        if (holo_ray_rect(shadow, scene->rects[i].corner,
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

HoloV3 holo_trace_ray(const HoloScene *scene, HoloRay ray) {
    /* The mirror walk, iterative because the GPU twin cannot recurse: at
       each surface the matte share of the light is banked and the mirrored
       share keeps travelling, tinted by the mirror's own color. When the
       ray escapes, whatever is still travelling collects the sky. */
    HoloV3 color = hv3(0, 0, 0);
    HoloV3 throughput = hv3(1, 1, 1);

    for (int bounce = 0; bounce <= HOLO_MAX_BOUNCE; bounce++) {
        HoloHit hit;
        HoloV3 albedo;
        float mirror;
        if (!nearest_hit(scene, ray, &hit, &albedo, &mirror)) {
            color = hv3_add(color, hv3_mul(throughput, sky(scene, ray.dir)));
            break;
        }

        float diffuse = hv3_dot(hit.normal, scene->sun_dir);
        if (diffuse < 0.0f) {
            diffuse = 0.0f;
        }
        if (diffuse > 0.0f && sun_blocked(scene, hit.point)) {
            diffuse = 0.0f;
        }
        /* Lambert under one sun, floored by the ambient stand-in: full sun
           gives exactly the albedo, full shadow exactly HOLO_AMBIENT of it.
           Only the matte share (1 - mirror) of the surface shades this way. */
        HoloV3 matte = hv3_scale(albedo,
                                 HOLO_AMBIENT + (1.0f - HOLO_AMBIENT) * diffuse);
        color = hv3_add(color, hv3_mul(throughput,
                                       hv3_scale(matte, 1.0f - mirror)));
        if (mirror <= 0.0f) {
            break;
        }

        throughput = hv3_mul(throughput, hv3_scale(albedo, mirror));
        ray.origin = hit.point;
        ray.dir = hv3_reflect(ray.dir, hit.normal);
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
