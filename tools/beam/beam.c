/* beam: the beam a Fresnel panel throws from a lamp at its focus, as data.
 *
 * The oracle answers "is the tracer right" and bench answers "what does it
 * cost". This answers a third question, the one a lighthouse asks: how wide
 * is the beam, and so how long is the flash. A rotating lighthouse optic is a
 * drum of bullseye panels around one lamp; each panel is a Fresnel lens with
 * the lamp at its focus, throwing a horizontal beam, and a mariner on the
 * water sees a flash each time a beam sweeps past. The published
 * characteristic gives the period between flashes and says nothing about
 * their length -- and the length is the beam's angular width divided by the
 * rate the drum turns, which is optics, not a choice.
 *
 * WHAT IT DOES. A lamp of finite size sits on the axis at the focal
 * distance, facing the lens; the lens is HoloFresnel, cut for that focus.
 * Every lamp point is paired with every point on the aperture, the ray
 * between them is followed through the rings with the engine's own
 * intersection and Fresnel functions -- refracted at each surface, its
 * transmitted share kept, reflected on total internal reflection, given up
 * as trapped after a dozen interactions -- and the direction it finally
 * leaves in is binned. That histogram IS the far-field beam: the intensity
 * a distant observer sees from each direction. Its width comes from two
 * things this frame has and a thin-lens formula does not: the lamp's size
 * seen from the lens, and each ring's tilt being exact only at its peak.
 *
 * WHAT IT DOES NOT DO. A first-order lighthouse lens is a dioptric belt
 * with catadioptric prism bands above and below; only the belt is here,
 * which costs flux and not width, since the bands throw the same beam. The
 * lamp is a flat disc facing the lens, where a real one is a filament or a
 * mantle; its size is an input and is usually the number nobody publishes.
 * And the light is traced at the D line -- the width of a green beam is
 * the width of a white one through green glass.
 *
 * The forward trace follows the transmitted share at every interface and
 * lets the reflected share go: a mirror image of the lamp in a facet is
 * stray light, not beam. The Fresnel losses are therefore in the numbers
 * as a total below one, and so is what the risers trap.
 *
 *   build\beam.exe --order 1 --panels 8 --period 5 --lamp 0.015
 *   build\beam.exe --order 4 --panels 4 --period 5 --json out.json
 *
 * --order N sets the focal distance a lens of that order is quoted at (the
 * standard series: 920 mm for a first order down to 150 mm for a sixth),
 * with a ring pitch and slab depth in proportion; --focal, --pitch, --rim
 * and --thick override any of them. --panels is how many bullseyes the drum
 * carries, which sets both the panel's width (its share of the drum) and,
 * with --period, how fast it turns. --lamp is the lamp's radius in metres.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hologram.h"
#include "source/scene_json.h"   /* holo_json_float */

#define PI 3.14159265358979f

/* The histogram: +-20 degrees at a tenth, both axes. */
#define HALF_DEG 20.0f
#define BIN_DEG 0.1f
#define BINS 401

typedef struct {
    int   order;
    float focal, rim, pitch, thick, ior;
    float lamp_r;
    int   panels;
    float period_s;
    int   lamp_n, ap_n;
} Spec;

/* The standard focal distances of the Fresnel orders, in metres. The rings'
   pitch and the slab's depth are scaled with the focal length so every order
   is the same lens at a different size; a cited pitch can override. */
static float order_focal(int order) {
    switch (order) {
    case 1: return 0.920f;
    case 2: return 0.700f;
    case 3: return 0.500f;
    case 4: return 0.250f;
    case 5: return 0.1875f;
    case 6: return 0.150f;
    default: return 0.0f;
    }
}

static float deg(float rad) { return rad * 180.0f / PI; }

/* Follow one ray from the lamp through the lens. Returns 1 with the exit
   direction and the transmitted weight, or 0 if the ray never came out
   (trapped) or never went in. */
static int through(const HoloFresnel *f, HoloRay ray, float *weight, HoloV3 *out) {
    int inside = 0;
    for (int step = 0; step < 12; step++) {
        HoloHit h;
        if (!holo_ray_fresnel(ray, f->center, f->axis, f->focal, f->ior,
                              f->r0, f->pitch, f->rim, f->thick, &h)) {
            if (step == 0) {
                return 0;   /* missed the lens entirely */
            }
            *out = ray.dir;
            return !inside;
        }
        /* Clamped: two unit vectors' dot product lands a hair past 1 in
           float on a head-on hit, and holo_fresnel's sqrt(1 - cos^2) then
           returns NaN, which would poison every total below. */
        float cos_i = -hv3_dot(h.normal, ray.dir);
        if (cos_i > 1.0f) cos_i = 1.0f;
        float n1 = inside ? f->ior : 1.0f;
        float n2 = inside ? 1.0f : f->ior;
        float rs, rp;
        holo_fresnel(cos_i, n1, n2, &rs, &rp);
        HoloV3 next;
        if (hv3_refract(ray.dir, h.normal, n1 / n2, &next)) {
            *weight *= 1.0f - 0.5f * (rs + rp);
            inside = !inside;
        } else {
            next = hv3_reflect(ray.dir, h.normal);   /* TIR: everything reflects */
        }
        ray.origin = h.point;
        ray.dir = next;
    }
    return 0;   /* still bouncing after twelve: trapped */
}

/* Full width of a profile at `frac` of its peak, in bins, by linear
   interpolation across the crossings either side of the peak. */
static float width_at(const double *prof, int n, double frac) {
    int peak = 0;
    for (int i = 1; i < n; i++) {
        if (prof[i] > prof[peak]) peak = i;
    }
    double level = frac * prof[peak];
    if (level <= 0.0) return 0.0f;
    double lo = peak, hi = peak;
    for (int i = peak; i > 0; i--) {
        if (prof[i - 1] < level) {
            lo = (i - 1) + (level - prof[i - 1]) / (prof[i] - prof[i - 1]);
            break;
        }
        lo = i - 1;
    }
    for (int i = peak; i + 1 < n; i++) {
        if (prof[i + 1] < level) {
            hi = i + (prof[i] - level) / (prof[i] - prof[i + 1]);
            break;
        }
        hi = i + 1;
    }
    return (float)(hi - lo) * BIN_DEG;
}

static void json_array(FILE *fp, const char *key, const double *v, int n,
                       double scale, const char *tail) {
    fprintf(fp, "  \"%s\": [", key);
    for (int i = 0; i < n; i++) {
        holo_json_float(fp, (float)(v[i] * scale));
        if (i + 1 < n) fputs(", ", fp);
    }
    fprintf(fp, "]%s", tail);
}

int main(int argc, char *argv[]) {
    Spec s = { .order = 1, .lamp_r = 0.015f, .panels = 8, .period_s = 5.0f,
               .ior = 1.52f, .lamp_n = 21, .ap_n = 201 };
    const char *json_path = 0;
    float focal_arg = 0, pitch_arg = 0, rim_arg = 0, thick_arg = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--order") == 0 && i + 1 < argc) s.order = atoi(argv[++i]);
        else if (strcmp(argv[i], "--focal") == 0 && i + 1 < argc) focal_arg = (float)atof(argv[++i]);
        else if (strcmp(argv[i], "--pitch") == 0 && i + 1 < argc) pitch_arg = (float)atof(argv[++i]);
        else if (strcmp(argv[i], "--rim") == 0 && i + 1 < argc) rim_arg = (float)atof(argv[++i]);
        else if (strcmp(argv[i], "--thick") == 0 && i + 1 < argc) thick_arg = (float)atof(argv[++i]);
        else if (strcmp(argv[i], "--ior") == 0 && i + 1 < argc) s.ior = (float)atof(argv[++i]);
        else if (strcmp(argv[i], "--lamp") == 0 && i + 1 < argc) s.lamp_r = (float)atof(argv[++i]);
        else if (strcmp(argv[i], "--panels") == 0 && i + 1 < argc) s.panels = atoi(argv[++i]);
        else if (strcmp(argv[i], "--period") == 0 && i + 1 < argc) s.period_s = (float)atof(argv[++i]);
        else if (strcmp(argv[i], "--lamp-n") == 0 && i + 1 < argc) s.lamp_n = atoi(argv[++i]);
        else if (strcmp(argv[i], "--ap-n") == 0 && i + 1 < argc) s.ap_n = atoi(argv[++i]);
        else if (strcmp(argv[i], "--json") == 0 && i + 1 < argc) json_path = argv[++i];
        else {
            fprintf(stderr, "beam: unknown argument %s\n", argv[i]);
            return 2;
        }
    }

    s.focal = focal_arg > 0 ? focal_arg : order_focal(s.order);
    if (s.focal <= 0) {
        fprintf(stderr, "beam: --order must be 1..6, or give --focal\n");
        return 2;
    }
    /* A panel is the bullseye's share of the drum: its half-width is what
       the focal distance subtends over half the panel's angle. */
    s.rim = rim_arg > 0 ? rim_arg : s.focal * tanf(PI / (float)s.panels);
    /* Rings about a thirtieth of the focal length, which is the coarse end
       of what a first order carries and keeps every order above HOLO_T_MIN
       by a wide margin; the slab three pitches deep, which clears the
       deepest groove for any usable rim. */
    s.pitch = pitch_arg > 0 ? pitch_arg : s.focal / 30.0f;
    s.thick = thick_arg > 0 ? thick_arg : 3.0f * s.pitch;

    float rim_max = s.focal * sqrtf(s.ior * s.ior - 1.0f);
    if (s.rim >= rim_max) {
        fprintf(stderr, "beam: rim %.3f m is past f sqrt(n^2 - 1) = %.3f m; "
                "the outer rings would not collimate\n", (double)s.rim, (double)rim_max);
        return 2;
    }

    HoloFresnel lens = {
        .center = hv3(0, 0, 0), .axis = hv3(0, 0, 1),
        .focal = s.focal, .r0 = 0.0f, .pitch = s.pitch, .rim = s.rim,
        .thick = s.thick, .albedo = hv3(1, 1, 1), .mirror = 0.0f,
        .transmit = 1.0f, .ior = s.ior, .disperse = 0.0f,
    };

    printf("beam: order %d, f %.3f m, rim %.3f m (%d panels), pitch %.4f m, "
           "slab %.3f m, n %.3f, lamp radius %.4f m\n",
           s.order, (double)s.focal, (double)s.rim, s.panels, (double)s.pitch,
           (double)s.thick, (double)s.ior, (double)s.lamp_r);

    static double hist[BINS][BINS];   /* [az][el] */
    double total = 0, binned = 0, stray = 0, lost = 0, missed = 0;
    long rays = 0;

    for (int qi = 0; qi < s.lamp_n; qi++) {
        for (int qj = 0; qj < s.lamp_n; qj++) {
            float qx = (((float)qi + 0.5f) / (float)s.lamp_n * 2.0f - 1.0f) * s.lamp_r;
            float qy = (((float)qj + 0.5f) / (float)s.lamp_n * 2.0f - 1.0f) * s.lamp_r;
            if (qx * qx + qy * qy > s.lamp_r * s.lamp_r) continue;
            HoloV3 q = hv3(qx, qy, s.focal);

            for (int pi = 0; pi < s.ap_n; pi++) {
                for (int pj = 0; pj < s.ap_n; pj++) {
                    float px = (((float)pi + 0.5f) / (float)s.ap_n * 2.0f - 1.0f) * s.rim;
                    float py = (((float)pj + 0.5f) / (float)s.ap_n * 2.0f - 1.0f) * s.rim;
                    if (px * px + py * py > s.rim * s.rim) continue;
                    HoloV3 p = hv3(px, py, 0);
                    HoloV3 to = hv3_sub(p, q);
                    float dist2 = hv3_dot(to, to);
                    HoloRay ray = { .origin = q, .dir = hv3_norm(to) };
                    /* A Lambertian lamp element into a patch of aperture:
                       both cosines are the ray's tilt from the axis. */
                    float w = ray.dir.z * ray.dir.z / dist2;
                    total += w;
                    rays++;

                    HoloV3 out;
                    if (!through(&lens, ray, &w, &out) || w != w) {
                        lost += w == w ? w : 0.0;   /* a NaN weight is lost, not counted */
                        continue;
                    }
                    if (out.z > 0.0f) {
                        stray += w;   /* back out the lamp's side */
                        continue;
                    }
                    float az = deg(atan2f(out.x, -out.z));
                    float el = deg(atan2f(out.y, sqrtf(out.x * out.x + out.z * out.z)));
                    int ia = (int)floorf((az + HALF_DEG) / BIN_DEG + 0.5f);
                    int ie = (int)floorf((el + HALF_DEG) / BIN_DEG + 0.5f);
                    if (ia < 0 || ia >= BINS || ie < 0 || ie >= BINS) {
                        stray += w;   /* off the histogram: not beam */
                        continue;
                    }
                    hist[ia][ie] += w;
                    binned += w;
                }
            }
        }
    }
    if (rays == 0 || total <= 0) {
        fprintf(stderr, "beam: nothing traced\n");
        return 1;
    }
    /* Note: `missed` stays zero by construction -- every aperture sample is
       inside the rim -- and is reported so a future change that lets one
       miss is visible. */

    /* An observer on the water is a point in direction (az, el), and what
       reaches it is the histogram there. The slices are averaged over half
       a degree either side of the axis so a tenth-degree bin's sampling
       noise does not set the width. */
    static double prof_az[BINS], prof_el[BINS];
    int band = (int)(0.5f / BIN_DEG);
    int mid = BINS / 2;
    for (int ia = 0; ia < BINS; ia++) {
        double acc = 0;
        for (int ie = mid - band; ie <= mid + band; ie++) acc += hist[ia][ie];
        prof_az[ia] = acc / (2 * band + 1);
    }
    for (int ie = 0; ie < BINS; ie++) {
        double acc = 0;
        for (int ia = mid - band; ia <= mid + band; ia++) acc += hist[ia][ie];
        prof_el[ie] = acc / (2 * band + 1);
    }
    double peak_az = 0, peak_el = 0;
    for (int i = 0; i < BINS; i++) {
        if (prof_az[i] > peak_az) peak_az = prof_az[i];
        if (prof_el[i] > peak_el) peak_el = prof_el[i];
    }
    float fwhm_az = width_at(prof_az, BINS, 0.5);
    float w5_az = width_at(prof_az, BINS, 0.05);
    float fwhm_el = width_at(prof_el, BINS, 0.5);
    float w5_el = width_at(prof_el, BINS, 0.05);

    /* The drum turns one panel's share of a turn per period, so the beam
       sweeps past a fixed observer at 360 / (panels * period) degrees a
       second, and the flash is as long as the beam is wide. */
    float sweep = 360.0f / ((float)s.panels * s.period_s);
    float flash_fwhm = fwhm_az / sweep, flash_w5 = w5_az / sweep;

    printf("  rays %ld: beam %.3f, stray %.3f, trapped %.3f, missed %.3f of the lamp's light\n",
           rays, binned / total, stray / total, lost / total, missed / total);
    printf("  horizontal: FWHM %.2f deg, full width at 5%% %.2f deg\n",
           (double)fwhm_az, (double)w5_az);
    printf("  vertical:   FWHM %.2f deg, full width at 5%% %.2f deg\n",
           (double)fwhm_el, (double)w5_el);
    printf("  %d panels every %.1f s sweep %.2f deg/s: the flash lasts %.3f s at half "
           "power, %.3f s to 5%% -- a duty cycle of %.3f, or %.3f\n",
           s.panels, (double)s.period_s, (double)sweep, (double)flash_fwhm,
           (double)flash_w5, (double)(flash_fwhm / s.period_s), (double)(flash_w5 / s.period_s));

    if (json_path) {
        FILE *fp = fopen(json_path, "wb");
        if (!fp) {
            fprintf(stderr, "beam: cannot write %s\n", json_path);
            return 1;
        }
        fputs("{\n  \"format\": \"hologram/beam/1\",\n", fp);
        fprintf(fp, "  \"inputs\": { \"order\": %d, \"focal_m\": ", s.order);
        holo_json_float(fp, s.focal);
        fputs(", \"rim_m\": ", fp); holo_json_float(fp, s.rim);
        fputs(", \"pitch_m\": ", fp); holo_json_float(fp, s.pitch);
        fputs(", \"thick_m\": ", fp); holo_json_float(fp, s.thick);
        fputs(", \"ior\": ", fp); holo_json_float(fp, s.ior);
        fputs(", \"lamp_radius_m\": ", fp); holo_json_float(fp, s.lamp_r);
        fprintf(fp, ", \"panels\": %d, \"period_s\": ", s.panels);
        holo_json_float(fp, s.period_s);
        fprintf(fp, ", \"lamp_grid\": %d, \"aperture_grid\": %d, \"rays\": %ld },\n",
                s.lamp_n, s.ap_n, rays);
        fputs("  \"light\": { \"beam\": ", fp); holo_json_float(fp, (float)(binned / total));
        fputs(", \"stray\": ", fp); holo_json_float(fp, (float)(stray / total));
        fputs(", \"trapped\": ", fp); holo_json_float(fp, (float)(lost / total));
        fputs(" },\n", fp);
        fprintf(fp, "  \"bin_deg\": %g, \"half_range_deg\": %g,\n", (double)BIN_DEG, (double)HALF_DEG);
        json_array(fp, "intensity_az", prof_az, BINS, peak_az > 0 ? 1.0 / peak_az : 0.0, ",\n");
        json_array(fp, "intensity_el", prof_el, BINS, peak_el > 0 ? 1.0 / peak_el : 0.0, ",\n");
        fputs("  \"horizontal\": { \"fwhm_deg\": ", fp); holo_json_float(fp, fwhm_az);
        fputs(", \"w5_deg\": ", fp); holo_json_float(fp, w5_az);
        fputs(" },\n  \"vertical\": { \"fwhm_deg\": ", fp); holo_json_float(fp, fwhm_el);
        fputs(", \"w5_deg\": ", fp); holo_json_float(fp, w5_el);
        fputs(" },\n  \"flash\": { \"sweep_deg_per_s\": ", fp); holo_json_float(fp, sweep);
        fputs(", \"fwhm_s\": ", fp); holo_json_float(fp, flash_fwhm);
        fputs(", \"w5_s\": ", fp); holo_json_float(fp, flash_w5);
        fputs(" }\n}\n", fp);
        fclose(fp);
        printf("  wrote %s\n", json_path);
    }
    return 0;
}
