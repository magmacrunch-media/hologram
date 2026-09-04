/* See scene_json.h. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>   /* strtof */
#include "scene_json.h"

/* The shortest of %.6g through %.9g that reads back as the same float.
   Nine significant digits always round-trip a binary32, so the loop is
   guaranteed to terminate with an exact spelling; starting at six is what
   keeps 0.3f in the file as "0.3" instead of "0.300000012", which is the
   difference between a scene that diffs and one that does not.

   A non-finite float has no JSON spelling at all -- there is no literal for
   NaN -- so it is written as 0. A scene carrying one is already broken; the
   file should still parse so the editor can show what it holds. */
static void wf(FILE *f, float v) {
    char buf[64];
    int p;
    if (!isfinite((double)v)) {
        fputc('0', f);
        return;
    }
    for (p = 6; p < 9; p++) {
        snprintf(buf, sizeof buf, "%.*g", p, (double)v);
        if (strtof(buf, 0) == v) {
            break;
        }
    }
    if (p == 9) {
        snprintf(buf, sizeof buf, "%.9g", (double)v);
    }
    fputs(buf, f);
}

static void wv3(FILE *f, HoloV3 v) {
    fputc('[', f);
    wf(f, v.x);
    fputs(", ", f);
    wf(f, v.y);
    fputs(", ", f);
    wf(f, v.z);
    fputc(']', f);
}

/* "  \"name\": <float>" and friends. The indent is spelled out at each call
   rather than tracked, because the nesting here is three deep and fixed. */
static void wkey(FILE *f, const char *indent, const char *name) {
    fprintf(f, "%s\"%s\": ", indent, name);
}

static void wfield_f(FILE *f, const char *indent, const char *name, float v,
                     const char *tail) {
    wkey(f, indent, name);
    wf(f, v);
    fputs(tail, f);
}

static void wfield_v3(FILE *f, const char *indent, const char *name, HoloV3 v,
                      const char *tail) {
    wkey(f, indent, name);
    wv3(f, v);
    fputs(tail, f);
}

static void write_sphere(FILE *f, const HoloSphere *s, const char *tail) {
    fputs("    {\n", f);
    wfield_v3(f, "      ", "center", s->center, ",\n");
    wfield_f(f, "      ", "radius", s->radius, ",\n");
    wfield_v3(f, "      ", "albedo", s->albedo, ",\n");
    wfield_f(f, "      ", "mirror", s->mirror, ",\n");
    wfield_f(f, "      ", "transmit", s->transmit, ",\n");
    wfield_f(f, "      ", "ior", s->ior, ",\n");
    wfield_f(f, "      ", "disperse", s->disperse, "\n");
    fprintf(f, "    }%s", tail);
}

static void write_rect(FILE *f, const HoloRect *r, const char *tail) {
    fputs("    {\n", f);
    wfield_v3(f, "      ", "corner", r->corner, ",\n");
    wfield_v3(f, "      ", "edge_u", r->edge_u, ",\n");
    wfield_v3(f, "      ", "edge_v", r->edge_v, ",\n");
    wfield_v3(f, "      ", "albedo", r->albedo, ",\n");
    wfield_f(f, "      ", "mirror", r->mirror, ",\n");
    wfield_f(f, "      ", "transmit", r->transmit, ",\n");
    wfield_f(f, "      ", "ior", r->ior, ",\n");
    wfield_f(f, "      ", "disperse", r->disperse, ",\n");
    fprintf(f, "      \"filter\": %d,\n", r->filter);
    wfield_f(f, "      ", "filter_angle", r->filter_angle, ",\n");
    wfield_f(f, "      ", "retard", r->retard, ",\n");
    wfield_f(f, "      ", "grating_period", r->grating_period, ",\n");
    wfield_f(f, "      ", "grating_angle", r->grating_angle, ",\n");
    wkey(f, "      ", "order_w");
    fputc('[', f);
    for (int i = 0; i < HOLO_GRATING_ORDERS; i++) {
        wf(f, r->order_w[i]);
        if (i + 1 < HOLO_GRATING_ORDERS) {
            fputs(", ", f);
        }
    }
    fputs("]\n", f);
    fprintf(f, "    }%s", tail);
}

static void write_dish(FILE *f, const HoloDish *d, const char *tail) {
    fputs("    {\n", f);
    wfield_v3(f, "      ", "apex", d->apex, ",\n");
    wfield_v3(f, "      ", "axis", d->axis, ",\n");
    wfield_f(f, "      ", "curv_r", d->curv_r, ",\n");
    wfield_f(f, "      ", "conic_k", d->conic_k, ",\n");
    wfield_f(f, "      ", "rim", d->rim, ",\n");
    wfield_v3(f, "      ", "albedo", d->albedo, ",\n");
    wfield_f(f, "      ", "mirror", d->mirror, "\n");
    fprintf(f, "    }%s", tail);
}

int holo_scene_write_json(const char *path, const HoloScene *scene,
                          const HoloCamera *cam, int spectral) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        return 0;
    }

    fputs("{\n", f);
    fprintf(f, "  \"format\": \"%s\",\n", HOLO_SCENE_JSON_FORMAT);
    fprintf(f, "  \"spectral\": %d,\n", spectral ? 1 : 0);

    /* The caps this scene was built against. A reader that knows them can
       say "23 of 24 rects" without being compiled against the engine. */
    fputs("  \"caps\": {\n", f);
    fprintf(f, "    \"spheres\": %d,\n", HOLO_MAX_SPHERES);
    fprintf(f, "    \"rects\": %d,\n", HOLO_MAX_RECTS);
    fprintf(f, "    \"dishes\": %d,\n", HOLO_MAX_DISHES);
    fprintf(f, "    \"gpu_gratings\": 2,\n");
    fprintf(f, "    \"bounce\": %d,\n", HOLO_MAX_BOUNCE);
    fprintf(f, "    \"rays\": %d\n", HOLO_MAX_RAYS);
    fputs("  },\n", f);

    /* The basis, not a target: see scene_json.h. fov_deg is a convenience
       for a reader that wants to show a number a person recognizes, and is
       derived from tan_half_fov, never the other way round. */
    fputs("  \"camera\": {\n", f);
    wfield_v3(f, "    ", "pos", cam->pos, ",\n");
    wfield_v3(f, "    ", "forward", cam->forward, ",\n");
    wfield_v3(f, "    ", "right", cam->right, ",\n");
    wfield_v3(f, "    ", "up", cam->up, ",\n");
    wfield_f(f, "    ", "tan_half_fov", cam->tan_half_fov, ",\n");
    wfield_f(f, "    ", "aspect", cam->aspect, ",\n");
    wfield_f(f, "    ", "fov_deg",
             (float)(2.0 * atan((double)cam->tan_half_fov) * 180.0 / 3.14159265358979),
             "\n");
    fputs("  },\n", f);

    fputs("  \"spheres\": [\n", f);
    for (int i = 0; i < scene->sphere_count; i++) {
        write_sphere(f, &scene->spheres[i],
                     i + 1 < scene->sphere_count ? ",\n" : "\n");
    }
    fputs("  ],\n", f);

    fputs("  \"rects\": [\n", f);
    for (int i = 0; i < scene->rect_count; i++) {
        write_rect(f, &scene->rects[i],
                   i + 1 < scene->rect_count ? ",\n" : "\n");
    }
    fputs("  ],\n", f);

    fputs("  \"dishes\": [\n", f);
    for (int i = 0; i < scene->dish_count; i++) {
        write_dish(f, &scene->dishes[i],
                   i + 1 < scene->dish_count ? ",\n" : "\n");
    }
    fputs("  ],\n", f);

    fputs("  \"floor\": {\n", f);
    fprintf(f, "    \"has_floor\": %d,\n", scene->has_floor ? 1 : 0);
    wfield_f(f, "    ", "floor_y", scene->floor_y, ",\n");
    wfield_v3(f, "    ", "floor_a", scene->floor_a, ",\n");
    wfield_v3(f, "    ", "floor_b", scene->floor_b, ",\n");
    wfield_f(f, "    ", "floor_mirror", scene->floor_mirror, "\n");
    fputs("  },\n", f);

    fputs("  \"sky\": {\n", f);
    wfield_v3(f, "    ", "sun_dir", scene->sun_dir, ",\n");
    wfield_v3(f, "    ", "horizon", scene->horizon, ",\n");
    wfield_v3(f, "    ", "zenith", scene->zenith, ",\n");
    wfield_f(f, "    ", "sun_disk_cos", scene->sun_disk_cos, ",\n");
    wfield_f(f, "    ", "sun_disk_intensity", scene->sun_disk_intensity, "\n");
    fputs("  }\n", f);

    fputs("}\n", f);

    return fclose(f) == 0;
}
