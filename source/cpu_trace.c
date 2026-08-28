/* See cpu_trace.h. */
#include <math.h>
#include "cpu_trace.h"
#include "polar.h"
#include "spectrum.h"

const int holo_grating_m[HOLO_GRATING_ORDERS] = { -1, 0, 1, 2 };

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
    int    filter;           /* HOLO_FILTER_*; rects only */
    int    rect;             /* which rect, for a filter's or grating's axis */
    float  grating;          /* period in um; 0 = not a grating */
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
            surf->filter = HOLO_FILTER_NONE;
            surf->rect = -1;
            surf->grating = 0.0f;
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
            surf->filter = scene->rects[i].filter;
            surf->rect = i;
            surf->grating = scene->rects[i].grating_period;
            found = 1;
        }
    }
    for (int i = 0; i < scene->dish_count; i++) {
        if (holo_ray_dish(ray, scene->dishes[i].apex, scene->dishes[i].axis,
                          scene->dishes[i].curv_r, scene->dishes[i].conic_k,
                          scene->dishes[i].rim, &h) && h.t < best.t) {
            best = h;
            surf->albedo = scene->dishes[i].albedo;
            surf->mirror = scene->dishes[i].mirror;
            surf->transmit = 0.0f;
            surf->ior = 1.0f;
            surf->disperse = 0.0f;
            surf->volume = 0;
            surf->filter = HOLO_FILTER_NONE;
            surf->rect = -1;
            surf->grating = 0.0f;
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
        surf->filter = HOLO_FILTER_NONE;
        surf->rect = -1;
        surf->grating = 0.0f;
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
            scene->rects[i].filter == HOLO_FILTER_NONE &&
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
    if (scene->sun_disk_intensity > 0.0f &&
        hv3_dot(dir, scene->sun_dir) > scene->sun_disk_cos) {
        float i = scene->sun_disk_intensity;
        return hv3(i, i, i);
    }
    return hv3_lerp(scene->horizon, scene->zenith, 0.5f * (dir.y + 1.0f));
}

static float max3(HoloV3 v) {
    float m = v.x > v.y ? v.x : v.y;
    return m > v.z ? m : v.z;
}

/* A ray still owed to the image: where it is going, how much of the pixel's
   light rides on it, whether it is currently inside glass. The spectral
   walk additionally carries the detector-row Stokes accumulator and the
   transverse frame its Q axis refers to; the RGB walk ignores both. */
typedef struct {
    HoloRay  ray;
    HoloV3   tp;
    int      inside;
    int      depth;
    int      order;   /* which grating order this visit emits; see below */
    HoloSRow srow;
    HoloV3   frame;
} PathRay;

/* Any unit transverse vector will do to start -- the first interface
   rotates it wherever it needs it. Shared verbatim with the shader. */
static HoloV3 initial_frame(HoloV3 dir) {
    HoloV3 f = hv3_cross(dir, hv3(0, 1, 0));
    if (hv3_dot(f, f) < 1e-6f) {
        f = hv3_cross(dir, hv3(1, 0, 0));
    }
    return hv3_norm(f);
}

/* A filter pane's axis on the transverse plane of the arriving ray: the
   axis in the pane, projected off the ray direction. Degenerate only when
   the pane is seen edge-on, when nothing is visible through it anyway. */
static HoloV3 filter_axis(const HoloRect *rect, HoloV3 dir, HoloV3 fallback) {
    HoloV3 in_pane = hv3_add(
        hv3_scale(hv3_norm(rect->edge_u), cosf(rect->filter_angle)),
        hv3_scale(hv3_norm(rect->edge_v), sinf(rect->filter_angle)));
    HoloV3 t = hv3_sub(in_pane, hv3_scale(dir, hv3_dot(in_pane, dir)));
    if (hv3_dot(t, t) < 1e-6f) {
        return fallback;
    }
    return hv3_norm(t);
}

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

        /* The RGB path has no wavelength, so a grating shows only its
           zeroth (specular) order at that order's weight. The spectral
           path fans the rest. */
        if (surf.grating > 0.0f) {
            HoloV3 tp = hv3_scale(p.tp, scene->rects[surf.rect].order_w[1]);
            if (max3(tp) > HOLO_MIN_TP && sp < HOLO_STACK &&
                p.depth < HOLO_MAX_BOUNCE) {
                stack[sp++] = (PathRay){
                    .ray = { .origin = hit.point,
                             .dir = hv3_reflect(p.ray.dir, hit.normal) },
                    .tp = tp, .inside = p.inside, .depth = p.depth + 1,
                };
            }
            continue;
        }

        /* The RGB path has no Stokes vector, so filters approximate: a
           polarizer is the 50% an unpolarized meter reads through one,
           a waveplate is clear glass. The spectral path does them right. */
        if (surf.filter != HOLO_FILTER_NONE) {
            float pass = surf.filter == HOLO_POLARIZER ? 0.5f : 1.0f;
            HoloV3 tp = hv3_scale(p.tp, pass);
            if (max3(tp) > HOLO_MIN_TP && sp < HOLO_STACK &&
                p.depth < HOLO_MAX_BOUNCE) {
                stack[sp++] = (PathRay){
                    .ray = { .origin = hit.point, .dir = p.ray.dir },
                    .tp = tp, .inside = p.inside, .depth = p.depth + 1,
                };
            }
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
                    int refracted = 1;
                    if (surf.volume) {
                        /* In or out through the surface; the ray changes
                           medium. Checked: at the razor edge of the
                           critical angle Fresnel's float arithmetic can
                           say "not TIR" while refract's disagrees. */
                        refracted = hv3_refract(p.ray.dir, hit.normal,
                                                n1 / n2, &t.ray.dir);
                        t.ray.origin = hit.point;
                        t.inside = !p.inside;
                    } else {
                        /* A thin pane: one Fresnel interface, no net bend,
                           same medium on both sides. */
                        t.ray = (HoloRay){ .origin = hit.point, .dir = p.ray.dir };
                        t.inside = p.inside;
                    }
                    if (refracted) {
                        stack[sp++] = t;
                    }
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
       cull -- carrying one wavelength's worth of light as a detector-row
       Stokes accumulator plus its transverse frame. Albedos are read at
       lambda, glass refracts at n(lambda), and every interface is a
       Mueller matrix in its own s/p or axis basis, reached by rotating the
       frame -- which is how crossed polarizers extinguish and Brewster
       reflections polarize with no special cases anywhere. Sources are
       unpolarized, so a path's contribution is source intensity times the
       row's first component. */
    /* CHAIN + FORK: each popped ray is walked as a chain, every surface
       continuing IN PLACE (reflection off glass and mirrors, transmission
       through filters, the current order off a grating) and forking AT
       MOST ONE side branch onto the stack (glass's transmitted ray, a
       grating's next-order revisit). One push per interaction is a hard
       ceiling: fxc corrupts the GPU walk's stack arrays when any single
       branch pushes twice, and the CPU mirrors the GPU walk exactly so
       the oracle diff stays meaningful. */
    PathRay stack[HOLO_STACK];
    int sp = 0, processed = 0;
    float intensity = 0.0f;

    stack[sp++] = (PathRay){ .ray = primary, .srow = holo_srow_start(),
                             .frame = initial_frame(primary.dir) };

    while (sp > 0 && processed < HOLO_MAX_RAYS) {
        PathRay p = stack[--sp];

        while (processed < HOLO_MAX_RAYS) {
            processed++;

            HoloHit hit;
            HoloSurface surf;
            if (!nearest_hit(scene, p.ray, &hit, &surf)) {
                intensity += p.srow.i
                           * holo_albedo_at(sky(scene, p.ray.dir), lambda_um);
                break;
            }

            /* A grating fans the ray into its propagating orders, the
               groove component conserved, each order weighted by its fixed
               efficiency. The chain follows THIS visit's order; the fork
               revisits the surface for the next. Weights are polarization-
               neutral scalars for now -- a grating's true polarization
               response is a solver's problem. */
            if (surf.grating > 0.0f) {
                if (p.depth >= HOLO_MAX_BOUNCE) {
                    break;
                }
                const HoloRect *rect = &scene->rects[surf.rect];
                HoloV3 groove = hv3_norm(hv3_add(
                    hv3_scale(hv3_norm(rect->edge_u),
                              cosf(rect->grating_angle)),
                    hv3_scale(hv3_norm(rect->edge_v),
                              sinf(rect->grating_angle))));
                if (p.order < HOLO_GRATING_ORDERS - 1 && sp < HOLO_STACK) {
                    PathRay revisit = p;
                    revisit.order = p.order + 1;
                    stack[sp++] = revisit;
                }
                HoloV3 out;
                if (!holo_grating_order(p.ray.dir, hit.normal, groove,
                        (float)holo_grating_m[p.order] * lambda_um
                            / surf.grating, &out)) {
                    break;   /* this order is evanescent */
                }
                HoloSRow s = holo_srow_scale(p.srow, rect->order_w[p.order]);
                if (s.i <= HOLO_MIN_TP) {
                    break;
                }
                p.ray = (HoloRay){ .origin = hit.point, .dir = out };
                p.srow = s;
                p.frame = initial_frame(out);
                p.depth++;
                p.order = 0;
                continue;
            }

            /* Filters: rotate into the pane's axis, apply its Mueller,
               carry the chain on straight. A waveplate's retardance scales
               as 1/lambda, so a thick one between crossed polarizers
               writes its interference colors through the spectral loop
               with no further help. */
            if (surf.filter != HOLO_FILTER_NONE) {
                if (p.depth >= HOLO_MAX_BOUNCE) {
                    break;
                }
                HoloV3 axis = filter_axis(&scene->rects[surf.rect],
                                          p.ray.dir, p.frame);
                float c2, s2;
                holo_frame_rot(p.frame, axis, p.ray.dir, &c2, &s2);
                HoloSRow s = holo_srow_rotate(p.srow, c2, s2);
                if (surf.filter == HOLO_POLARIZER) {
                    s = holo_srow_polarizer(s);
                } else {
                    float d = scene->rects[surf.rect].retard
                            * 0.5893f / lambda_um;
                    s = holo_srow_mueller(s, 1, 0, cosf(d), sinf(d));
                }
                if (s.i <= HOLO_MIN_TP) {
                    break;
                }
                p.ray.origin = hit.point;
                p.srow = s;
                p.frame = axis;
                p.depth++;
                p.order = 0;
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
                              * (HOLO_AMBIENT
                                 + (1.0f - HOLO_AMBIENT) * diffuse);
                intensity += p.srow.i * lambert * matte;
            }

            if (p.depth >= HOLO_MAX_BOUNCE) {
                break;
            }

            /* The s vector of the plane of incidence is the basis both
               Fresnel branches speak; at normal incidence there is no
               plane and no rotation to do. */
            HoloV3 s_hat = hv3_cross(p.ray.dir, hit.normal);
            float c2 = 1.0f, s2 = 0.0f;
            if (hv3_dot(s_hat, s_hat) > 1e-6f) {
                s_hat = hv3_norm(s_hat);
                holo_frame_rot(p.frame, s_hat, p.ray.dir, &c2, &s2);
            } else {
                s_hat = p.frame;
            }
            HoloSRow srow_sp = holo_srow_rotate(p.srow, c2, s2);

            float mtint = holo_albedo_at(surf.albedo, lambda_um)
                        * surf.mirror;
            float ra = mtint, rb = 0.0f, rc = mtint, rd = 0.0f;

            if (surf.transmit > 0.0f) {
                float n_glass = holo_ior_at(surf.ior, surf.disperse,
                                            lambda_um);
                float cos_i = -hv3_dot(hit.normal, p.ray.dir);
                float n1 = p.inside ? n_glass : 1.0f;
                float n2 = p.inside ? 1.0f : n_glass;
                float rs, rp, ts, tp_amp, f, delta;
                int tir = holo_fresnel_amp(cos_i, n1, n2, &rs, &rp,
                                           &ts, &tp_amp, &f, &delta);

                if (!tir) {
                    /* The fork: the transmitted ray. */
                    float k = holo_albedo_at(surf.albedo, lambda_um)
                            * surf.transmit;
                    HoloSRow st = holo_srow_mueller(srow_sp,
                            k * 0.5f * f * (ts * ts + tp_amp * tp_amp),
                            k * 0.5f * f * (ts * ts - tp_amp * tp_amp),
                            k * f * ts * tp_amp, 0.0f);
                    if (st.i > HOLO_MIN_TP && sp < HOLO_STACK) {
                        PathRay t = { .depth = p.depth + 1, .srow = st,
                                      .frame = s_hat };
                        int refracted = 1;
                        if (surf.volume) {
                            /* Checked: at the razor edge of the critical
                               angle, Fresnel's float arithmetic can say
                               "not TIR" while refract's says otherwise --
                               an unchecked call here once pushed an
                               uninitialized direction. */
                            refracted = hv3_refract(p.ray.dir, hit.normal,
                                                    n1 / n2, &t.ray.dir);
                            t.ray.origin = hit.point;
                            t.inside = !p.inside;
                        } else {
                            t.ray = (HoloRay){ .origin = hit.point,
                                               .dir = p.ray.dir };
                            t.inside = p.inside;
                        }
                        if (refracted) {
                            stack[sp++] = t;
                        }
                    }
                    ra += surf.transmit * 0.5f * (rs * rs + rp * rp);
                    rb += surf.transmit * 0.5f * (rs * rs - rp * rp);
                    rc += surf.transmit * rs * rp;
                } else {
                    /* TIR: everything reflects, p delayed against s --
                       real retardance, the kind a Fresnel rhomb is cut
                       to use. */
                    ra += surf.transmit;
                    rc += surf.transmit * cosf(delta);
                    rd += surf.transmit * sinf(delta);
                }
            }

            /* The chain: the reflected ray. */
            HoloSRow sr = holo_srow_mueller(srow_sp, ra, rb, rc, rd);
            if (sr.i <= HOLO_MIN_TP) {
                break;
            }
            p.ray = (HoloRay){ .origin = hit.point,
                               .dir = hv3_reflect(p.ray.dir, hit.normal) };
            p.srow = sr;
            p.frame = s_hat;
            p.depth++;
            p.order = 0;
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
