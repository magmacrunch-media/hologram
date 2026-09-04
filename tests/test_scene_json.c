/* holo_scene_write_json writes a scene the editor opens, so what it owes is
 * exactness: a float that leaves the struct must come back as the same
 * float, or a scene edited and emitted back to C is quietly not the scene
 * that was rendered.
 *
 * There is no reader to test it against -- scene_json.h explains why the
 * engine will not gain one -- so this reads the text back and pulls the
 * numbers out with strtof, which is the same function the shortest-spelling
 * loop in the writer checks itself against. Round-tripping through it here
 * is therefore the real property, not a restatement of the implementation:
 * if the loop ever stops early, these comparisons stop being equal.
 *
 * Like test_gpu_layout, it writes and reads a file, so it wants the working
 * directory the build scripts already use: the repository root.
 */
#include <stdio.h>
#include <stdlib.h>   /* strtof */
#include <string.h>
#include "harness.h"
#include "scene_json.h"

#define PATH "build/test_scene_json.json"

static char text[128 * 1024];

/* The value after the first "key": at or past `from`. Keys repeat across
   primitives (every one of them has an albedo), so the caller walks a
   cursor forward rather than searching the whole file each time. */
static const char *find_key(const char *from, const char *key) {
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\": ", key);
    const char *p = strstr(from, pat);
    return p ? p + strlen(pat) : 0;
}

/* Exact float equality, which is the whole point here: check_close's 1e-4
   would pass a spelling that lost three digits. */
static void check_exact(const char *from, const char *key, float want,
                        const char *what) {
    const char *p = find_key(from, key);
    if (!p) {
        check(0, what);
        return;
    }
    float got = strtof(p, 0);
    checks++;
    if (got != want) {
        printf("  FAIL: %s (got %.9g, want %.9g)\n", what,
               (double)got, (double)want);
        failures++;
    }
}

static void check_int_key(const char *from, const char *key, int want,
                          const char *what) {
    const char *p = find_key(from, key);
    if (!p) {
        check(0, what);
        return;
    }
    check_int((int)strtol(p, 0, 10), want, what);
}

/* The start of a primitive list. Searched as "name": [ rather than "name",
   because the caps block names every list too and sits above all of them --
   a bare "dishes" finds the budget, not the dish. */
static const char *find_list(const char *name) {
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\": [", name);
    return strstr(text, pat);
}

static int count_of(const char *key) {
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\": ", key);
    int n = 0;
    for (const char *p = strstr(text, pat); p; p = strstr(p + 1, pat)) {
        n++;
    }
    return n;
}

/* Read the whole file into text[], NUL-terminated. Returns its length, or 0
   if it could not be read. One reader for both scenes below -- two would be
   two chances to forget the terminator. */
static size_t slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return 0;
    }
    size_t n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    return n;
}

/* Structurally a JSON document: every brace and bracket closed, and none
   closed before it was opened. Cheap, and it catches a trailing comma's
   cousin -- a list left open by a count that walked off the end. Returns 1
   when balanced; `label` names the scene in any failure. */
static int balanced(size_t n, const char *label) {
    int braces = 0, brackets = 0, underflow = 0;
    char what[64];
    for (size_t i = 0; i < n; i++) {
        if (text[i] == '{') braces++;
        if (text[i] == '}') braces--;
        if (text[i] == '[') brackets++;
        if (text[i] == ']') brackets--;
        if (braces < 0 || brackets < 0) underflow = 1;
    }
    snprintf(what, sizeof what, "%s: braces balance", label);
    check_int(braces, 0, what);
    snprintf(what, sizeof what, "%s: brackets balance", label);
    check_int(brackets, 0, what);
    snprintf(what, sizeof what, "%s: never closes more than it opened", label);
    check_int(underflow, 0, what);
    return braces == 0 && brackets == 0 && !underflow;
}

int main(void) {
    HoloScene scene = (HoloScene){ 0 };

    /* Values chosen to exercise the spelling loop from both ends: 0.75 and
       1.2 are short, 1.0f/3.0f needs every digit binary32 has. */
    scene.sphere_count = 1;
    scene.spheres[0] = (HoloSphere){
        .center = hv3(1.5f, 0.75f, -2.0f), .radius = 0.75f,
        .albedo = hv3(0.8f, 0.2f, 0.1f), .mirror = 0.0f,
        .transmit = 0.9f, .ior = 1.5168f, .disperse = 0.0042f,
    };

    scene.rect_count = 2;
    /* A polarizer, whose axis angle the GPU block turns into a vector but
       the file must keep as the angle the scene was written with. */
    scene.rects[0] = (HoloRect){
        .corner = hv3(-1, 0, 0), .edge_u = hv3(2, 0, 0), .edge_v = hv3(0, 2, 0),
        .albedo = hv3(1, 1, 1),
        .filter = HOLO_POLARIZER, .filter_angle = 1.0f / 3.0f,
    };
    /* A grating: 1.2 um is the spectroscopist's 833 lines/mm from the
       header, and the four order weights must survive in order. */
    scene.rects[1] = (HoloRect){
        .corner = hv3(-1, 0, -3), .edge_u = hv3(2, 0, 0), .edge_v = hv3(0, 2, 0),
        .albedo = hv3(0.5f, 0.5f, 0.5f),
        .grating_period = 1.2f, .grating_angle = 0.25f,
        .order_w = { 0.1f, 0.6f, 0.2f, 0.05f },
    };

    scene.dish_count = 1;
    scene.dishes[0] = (HoloDish){
        .apex = hv3(0, 0, -6), .axis = hv3(0, 0, 1),
        .curv_r = 4.0f, .conic_k = -1.0f, .rim = 1.5f,
        .albedo = hv3(0.9f, 0.9f, 0.9f), .mirror = 0.95f,
    };

    scene.has_floor = 1;
    scene.floor_y = -0.5f;
    scene.floor_a = hv3(0.2f, 0.2f, 0.2f);
    scene.floor_b = hv3(0.7f, 0.7f, 0.7f);
    scene.floor_mirror = 0.1f;
    scene.sun_dir = hv3(0.169f, 0.507f, 0.845f);
    scene.horizon = hv3(0.6f, 0.7f, 0.9f);
    scene.zenith = hv3(0.2f, 0.4f, 0.8f);
    scene.sun_disk_cos = 0.9995f;
    scene.sun_disk_intensity = 40.0f;

    HoloCamera cam = holo_camera_make(hv3(0, 1, 5), hv3(0, 1, 0), hv3(0, 1, 0),
                                      60.0f, 4.0f / 3.0f);

    check(holo_scene_write_json(PATH, &scene, &cam, 1), "write succeeds");

    size_t n = slurp(PATH);
    check(n > 0 && n < sizeof text - 1, "file reads back, non-empty and fits");
    if (!n) {
        return report();
    }

    balanced(n, "scene");
    check(strstr(text, ",\n  }") == 0 && strstr(text, ",\n  ]") == 0,
          "no trailing comma before a close");

    check(strstr(text, "\"" "format" "\": \"" HOLO_SCENE_JSON_FORMAT "\"") != 0,
          "format string is written");
    check_int_key(text, "spectral", 1, "spectral flag");

    /* The caps travel with the scene so a reader can show a budget without
       being compiled against the engine. */
    const char *caps = strstr(text, "\"caps\"");
    check(caps != 0, "caps block exists");
    if (caps) {
        check_int_key(caps, "rects", HOLO_MAX_RECTS, "cap: rects");
        check_int_key(caps, "spheres", HOLO_MAX_SPHERES, "cap: spheres");
        check_int_key(caps, "dishes", HOLO_MAX_DISHES, "cap: dishes");
        check_int_key(caps, "gpu_gratings", 2, "cap: gpu gratings");
        check_int_key(caps, "bounce", HOLO_MAX_BOUNCE, "cap: bounce");
        check_int_key(caps, "rays", HOLO_MAX_RAYS, "cap: rays");
    }

    /* The camera as basis, exactly as it reaches the block. */
    const char *c = strstr(text, "\"camera\"");
    check(c != 0, "camera block exists");
    if (c) {
        check_exact(c, "tan_half_fov", cam.tan_half_fov, "camera tan_half_fov");
        check_exact(c, "aspect", cam.aspect, "camera aspect");
        /* forward is (0,0,-1) here, so the basis is checkable by eye. */
        check(strstr(c, "\"forward\": [0, 0, -1]") != 0, "camera forward");
        const char *fov = find_key(c, "fov_deg");
        check(fov != 0, "camera fov_deg is written");
        if (fov) {
            /* Derived from tan_half_fov on the way out, so it is checked
               loosely: it is a convenience for a reader, not the source. */
            check_close(strtof(fov, 0), 60.0f, "camera fov_deg");
        }
    }

    /* One of each primitive, and the counts implied by the arrays. */
    check_int(count_of("radius"), 1, "one sphere written");
    check_int(count_of("corner"), 2, "two rects written");
    check_int(count_of("curv_r"), 1, "one dish written");

    const char *sph = find_list("spheres");
    check(sph != 0, "spheres block exists");
    if (sph) {
        check_exact(sph, "radius", 0.75f, "sphere radius round-trips");
        check_exact(sph, "ior", 1.5168f, "sphere ior round-trips");
        check_exact(sph, "disperse", 0.0042f, "sphere disperse round-trips");
        check_exact(sph, "transmit", 0.9f, "sphere transmit round-trips");
    }

    const char *r0 = find_list("rects");
    check(r0 != 0, "rects block exists");
    if (r0) {
        check_int_key(r0, "filter", HOLO_POLARIZER, "rect 0 is a polarizer");
        /* The awkward one: 1/3 has no short decimal spelling, so this is
           what fails first if the writer ever stops at %.6g. */
        check_exact(r0, "filter_angle", 1.0f / 3.0f,
                    "rect 0 filter_angle round-trips");

        /* Into the second rect. Every key repeats per primitive, so the
           cursor has to walk past the first one's corner to read the
           second's anything -- rect 0's grating_period is a real 0. */
        const char *r1 = find_key(r0, "corner");
        r1 = r1 ? find_key(r1, "corner") : 0;
        check(r1 != 0, "a second rect was written");
        if (r1) {
            check_int_key(r1, "filter", HOLO_FILTER_NONE, "rect 1 has no filter");
            check_exact(r1, "grating_period", 1.2f, "grating period");
            check_exact(r1, "grating_angle", 0.25f, "grating angle");
            /* Order weights are the one array whose ORDER carries meaning:
               m = -1, 0, +1, +2. A reversed list renders a plausible and
               wrong spectrum, so it is checked as written text. */
            check(strstr(r1, "\"order_w\": [0.1, 0.6, 0.2, 0.05]") != 0,
                  "order weights in m = -1, 0, +1, +2 order");
        }
    }

    const char *d = find_list("dishes");
    check(d != 0, "dishes block exists");
    if (d) {
        check_exact(d, "curv_r", 4.0f, "dish curv_r");
        check_exact(d, "conic_k", -1.0f, "dish conic_k (paraboloid)");
        check_exact(d, "rim", 1.5f, "dish rim");
        check_exact(d, "mirror", 0.95f, "dish mirror");
    }

    const char *fl = strstr(text, "\"floor\"");
    check(fl != 0, "floor block exists");
    if (fl) {
        check_int_key(fl, "has_floor", 1, "floor is on");
        check_exact(fl, "floor_y", -0.5f, "floor y");
        check_exact(fl, "floor_mirror", 0.1f, "floor mirror");
    }

    const char *sky = strstr(text, "\"sky\"");
    check(sky != 0, "sky block exists");
    if (sky) {
        check_exact(sky, "sun_disk_cos", 0.9995f, "sun disk cos");
        check_exact(sky, "sun_disk_intensity", 40.0f, "sun disk intensity");
        check(strstr(sky, "\"sun_dir\": [0.169, 0.507, 0.845]") != 0,
              "sun direction keeps its short spelling");
    }

    /* An empty scene must still be a document, not a file with a dangling
       comma where the spheres would have been. */
    HoloScene empty = (HoloScene){ 0 };
    check(holo_scene_write_json(PATH, &empty, &cam, 0), "empty scene writes");
    n = slurp(PATH);
    check(n > 0, "empty scene reads back");
    if (n) {
        balanced(n, "empty scene");
        check(strstr(text, "\"spheres\": [\n  ]") != 0,
              "empty scene has an empty spheres list");
        check(strstr(text, "\"rects\": [\n  ]") != 0,
              "empty scene has an empty rects list");
        check_int_key(text, "spectral", 0, "empty scene is not spectral");
    }

    return report();
}
