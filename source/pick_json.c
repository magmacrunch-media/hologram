/* See pick_json.h. */
#include <stdio.h>
#include "geometry.h"
#include "pick_json.h"
#include "scene_json.h"   /* holo_json_float */

int holo_pick_ray(const HoloScene *scene, HoloRay ray, int *index) {
    HoloHit best = { .t = 1e30f };
    HoloHit h;
    int kind = HOLO_PICK_NONE;
    int idx = -1;

    /* The order and the strict comparison are cpu_trace.c's nearest_hit,
       deliberately. Where two surfaces touch, whichever is tested first wins
       an exact tie, and a picker that disagreed about that would select a
       different thing along every shared edge in the room. */
    for (int i = 0; i < scene->sphere_count; i++) {
        if (holo_ray_sphere(ray, scene->spheres[i].center,
                            scene->spheres[i].radius, &h) && h.t < best.t) {
            best = h;
            kind = HOLO_PICK_SPHERE;
            idx = i;
        }
    }
    for (int i = 0; i < scene->rect_count; i++) {
        if (holo_ray_rect(ray, scene->rects[i].corner, scene->rects[i].edge_u,
                          scene->rects[i].edge_v, &h) && h.t < best.t) {
            best = h;
            kind = HOLO_PICK_RECT;
            idx = i;
        }
    }
    for (int i = 0; i < scene->dish_count; i++) {
        if (holo_ray_dish(ray, scene->dishes[i].apex, scene->dishes[i].axis,
                          scene->dishes[i].curv_r, scene->dishes[i].conic_k,
                          scene->dishes[i].rim, &h) && h.t < best.t) {
            best = h;
            kind = HOLO_PICK_DISH;
            idx = i;
        }
    }
    if (scene->has_floor &&
        holo_ray_plane(ray, hv3(0, scene->floor_y, 0), hv3(0, 1, 0), &h) &&
        h.t < best.t) {
        kind = HOLO_PICK_FLOOR;
        idx = -1;
    }

    *index = idx;
    return kind;
}

int holo_pick_write_json(const char *path, const HoloScene *scene,
                         const HoloCamera *cam) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        return 0;
    }

    fputs("{\n", f);
    fprintf(f, "  \"format\": \"%s\",\n", HOLO_PICK_JSON_FORMAT);
    fprintf(f, "  \"grid\": { \"cols\": %d, \"rows\": %d },\n",
            HOLO_PICK_COLS, HOLO_PICK_ROWS);
    fputs("  \"kinds\": [\"none\", \"sphere\", \"rect\", \"dish\", \"floor\"],\n", f);

    /* The camera the grid was cast through, so a reader need not be told it
       separately and cannot use the wrong one by accident. */
    fputs("  \"camera\": {\n    \"pos\": [", f);
    holo_json_float(f, cam->pos.x); fputs(", ", f);
    holo_json_float(f, cam->pos.y); fputs(", ", f);
    holo_json_float(f, cam->pos.z);
    fputs("],\n    \"forward\": [", f);
    holo_json_float(f, cam->forward.x); fputs(", ", f);
    holo_json_float(f, cam->forward.y); fputs(", ", f);
    holo_json_float(f, cam->forward.z);
    fputs("],\n    \"right\": [", f);
    holo_json_float(f, cam->right.x); fputs(", ", f);
    holo_json_float(f, cam->right.y); fputs(", ", f);
    holo_json_float(f, cam->right.z);
    fputs("],\n    \"up\": [", f);
    holo_json_float(f, cam->up.x); fputs(", ", f);
    holo_json_float(f, cam->up.y); fputs(", ", f);
    holo_json_float(f, cam->up.z);
    fputs("],\n    \"tan_half_fov\": ", f);
    holo_json_float(f, cam->tan_half_fov);
    fputs(",\n    \"aspect\": ", f);
    holo_json_float(f, cam->aspect);
    fputs("\n  },\n", f);

    /* Flat pairs: kind, index, kind, index... Row-major from the top, at
       pixel centres, the way holo_trace_image samples. */
    fputs("  \"hits\": [\n", f);
    int total = HOLO_PICK_COLS * HOLO_PICK_ROWS;
    int written = 0;
    for (int y = 0; y < HOLO_PICK_ROWS; y++) {
        fputs("    ", f);
        for (int x = 0; x < HOLO_PICK_COLS; x++) {
            float u = ((float)x + 0.5f) / (float)HOLO_PICK_COLS;
            float v = ((float)y + 0.5f) / (float)HOLO_PICK_ROWS;
            int index;
            int kind = holo_pick_ray(scene, holo_camera_ray(cam, u, v), &index);
            fprintf(f, "%d,%d%s", kind, index,
                    ++written < total ? ", " : "");
        }
        fputs("\n", f);
    }
    fputs("  ]\n}\n", f);

    return fclose(f) == 0;
}
