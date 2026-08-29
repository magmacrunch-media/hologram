/* The GLSL tracer reads the uniform block as one array of vec4 and names its
 * fields with accessor macros over slot numbers -- see the header of
 * shaders/trace.glsl for why it has to (sokol's GL backend flattens a uniform
 * block into at most 16 named uniforms, and HoloGpuScene has closer to
 * thirty fields).
 *
 * That makes the slot numbers a second, hand-written copy of HoloGpuScene's
 * layout, and a copy that no compiler checks. This test is the check: it
 * parses the #define lines out of shaders/trace.glsl and holds every one of
 * them to offsetof(HoloGpuScene, ...) / 16. Reorder the struct, or add a
 * field in the middle of it, and this fails naming the slot that moved --
 * rather than the GPU quietly reading the camera out of the sun.
 *
 * It is the only test that reads a file, so it wants the working directory
 * the build scripts already use: the repository root.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "harness.h"
#include "gpu_scene.h"

#define SHADER_PATH "shaders/trace.glsl"

typedef struct {
    const char *name;
    size_t      slot;      /* what the C struct says */
    int         seen;      /* whether the GLSL mentioned it */
} Field;

/* Every accessor in trace.glsl that names a HoloGpuScene field. res and time
   live in the display header at slot 0 and are checked separately. */
#define F(field) { #field, offsetof(HoloGpuScene, field) / 16, 0 }

static Field fields[] = {
    F(cam_pos), F(tan_half_fov), F(cam_fwd), F(spectral),
    F(cam_right), F(sphere_count), F(cam_up), F(has_floor),
    F(sun_dir), F(floor_y), F(horizon), F(rect_count),
    F(zenith), F(floor_mirror), F(floor_a), F(sun_disk_cos),
    F(floor_b), F(sun_disk_intensity),
    F(sph_center_radius), F(sph_albedo_mirror), F(sph_glass),
    F(rect_corner_mirror), F(rect_solve_u), F(rect_solve_v),
    F(rect_albedo), F(rect_glass), F(rect_filter),
    F(dish_apex_r), F(dish_axis_k), F(dish_albedo_mirror), F(dish_rim_count),
    F(spectral_lw),
    F(grat0_groove_idx), F(grat0_period_w),
    F(grat1_groove_idx), F(grat1_period_w), F(grat_w2),
};

static const int FIELD_COUNT = (int)(sizeof fields / sizeof fields[0]);

/* "#define name  params[N]..." or "#define name(i)  params[N + (i)]" --
   returns the name and N, or 0 if the line is not an accessor. */
static int parse_accessor(const char *line, char *name, size_t name_size,
                          long *slot) {
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (strncmp(p, "#define ", 8) != 0) return 0;
    p += 8;
    while (*p == ' ') p++;

    size_t n = 0;
    while (*p && *p != ' ' && *p != '\t' && *p != '(') {
        if (n + 1 >= name_size) return 0;
        name[n++] = *p++;
    }
    name[n] = 0;
    if (n == 0) return 0;

    const char *br = strstr(p, "params[");
    if (!br) return 0;
    br += 7;
    char *end;
    long v = strtol(br, &end, 10);
    if (end == br) return 0;
    *slot = v;
    return 1;
}

int main(void) {
    printf("gpu layout: the block is a whole number of float4 slots\n");
    check(sizeof(HoloGpuScene) % 16 == 0,
          "sizeof(HoloGpuScene) is a multiple of 16 -- the GL path uploads "
          "it as an array of vec4");
    check(offsetof(HoloGpuScene, display) == 0,
          "the display header leads the block, as display.c assumes");

    printf("gpu layout: trace.glsl's slots against the struct\n");
    FILE *f = fopen(SHADER_PATH, "rb");
    if (!f) {
        check(0, "could not open " SHADER_PATH " -- run this from the "
                 "repository root, as build.bat and build.sh do");
        return report();
    }

    char line[512], name[128];
    long slot;
    long array_len = -1;
    while (fgets(line, sizeof line, f)) {
        /* the declaration itself: uniform vec4 params[N]; */
        const char *decl = strstr(line, "uniform vec4 params[");
        if (decl) {
            array_len = strtol(decl + 20, NULL, 10);
            continue;
        }
        if (!parse_accessor(line, name, sizeof name, &slot)) continue;
        for (int i = 0; i < FIELD_COUNT; i++) {
            if (strcmp(name, fields[i].name) != 0) continue;
            fields[i].seen = 1;
            char what[256];
            snprintf(what, sizeof what,
                     "trace.glsl reads %s from slot %ld; the struct puts it "
                     "at slot %zu", name, slot, fields[i].slot);
            check(slot == (long)fields[i].slot, what);
        }
    }
    fclose(f);

    check(array_len == (long)(sizeof(HoloGpuScene) / 16),
          "trace.glsl declares params[] the same length as the struct");

    for (int i = 0; i < FIELD_COUNT; i++) {
        char what[256];
        snprintf(what, sizeof what,
                 "trace.glsl still has an accessor for %s", fields[i].name);
        check(fields[i].seen, what);
    }

    return report();
}
