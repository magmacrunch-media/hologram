/* See walk_json.h. */
#include "walk_json.h"
#include "scene_json.h"   /* holo_json_float */

/* The script. Six phases, each a horizontal velocity held for a number of
   steps, with an optional jump on the phase's first step.

   The speeds are faster than a game walks on purpose: the point is to reach
   a wall inside a few hundred steps from wherever the example happens to
   start its player, and a room is a few metres across. */
#define WALK_DT   (1.0f / 120.0f)
#define WALK_FAST 8.0f
#define PHASE_STEPS 110

typedef struct {
    float vx, vz;
    float jump;      /* upward velocity applied on the phase's first step */
} WalkPhase;

static const WalkPhase PHASES[] = {
    {  WALK_FAST,        0.0f, 0.0f },   /* into +x until something stops it */
    { -WALK_FAST,        0.0f, 0.0f },   /* and back out, then -x */
    {       0.0f,  WALK_FAST, 0.0f },    /* +z */
    {       0.0f, -WALK_FAST, 0.0f },    /* -z */
    {  WALK_FAST,  WALK_FAST, 0.0f },    /* the diagonal: must slide, not stick */
    {       0.0f,       0.0f, 6.0f },    /* jump, and fall back to the floor */
};

#define PHASE_COUNT ((int)(sizeof PHASES / sizeof PHASES[0]))

static void wv3(FILE *f, HoloV3 v) {
    fputc('[', f);
    holo_json_float(f, v.x);
    fputs(", ", f);
    holo_json_float(f, v.y);
    fputs(", ", f);
    holo_json_float(f, v.z);
    fputc(']', f);
}

/* The example worlds are rooms, and a room does not exercise everything a
   walk step does: every wall in one starts at the floor, so the vertical
   span test never changes its answer, and a scripted walk only meets the
   walls it happens to pass. Both of those are real holes -- a reader whose
   span test is wrong walks through an overhang and the room trace never
   notices.

   So there is a second world, built here rather than dumped from a game,
   whose whole purpose is to be awkward:

     A  a plain wall, for the ordinary clamp
     B  a curb, knee high -- blocks you on the ground, and must NOT block
        you once a jump has lifted your feet above it
     C  an overhang starting at 1.65 -- blocks a 1.7 walker by a hand's
        breadth, and would let a shorter one pass. Nothing else in the repo
        makes `height` change an outcome.

   Written to its own file by whichever example dumps; they all write the
   same thing, which is the point of it not depending on the game. */
static void selftest_world(HoloWalkWorld *w) {
    *w = (HoloWalkWorld){ 0 };
    w->radius = 0.3f;
    w->height = 1.7f;
    w->gravity = 20.0f;
    w->floor_y = 0.0f;
    w->wall_count = 3;
    /* A: a plain wall across +x. */
    w->walls[0] = (HoloAabb){ .min = { 3.0f, 0.0f, -4.0f },
                              .max = { 3.4f, 3.0f, 4.0f } };
    /* B: a curb across +z, 0.4 high. */
    w->walls[1] = (HoloAabb){ .min = { -4.0f, 0.0f, 2.0f },
                              .max = { 2.0f, 0.4f, 2.4f } };
    /* C: an overhang across -z, its underside at 1.65. */
    w->walls[2] = (HoloAabb){ .min = { -4.0f, 1.65f, -2.4f },
                              .max = { 2.0f, 3.0f, -2.0f } };
}

/* The curb phase has to jump first and travel while airborne, so the
   selftest runs its own script rather than PHASES. */
typedef struct {
    float vx, vz, jump;
    int steps;
} SelfPhase;

static const SelfPhase SELF_PHASES[] = {
    {  4.0f,  0.0f, 0.0f, 130 },   /* into wall A, and stop at 3.0 - 0.3 */
    { -4.0f,  0.0f, 0.0f,  60 },   /* back off */
    {  0.0f,  4.0f, 0.0f, 110 },   /* into the curb on foot: blocked at 1.7 */
    {  0.0f,  0.0f, 7.0f,  10 },   /* jump on the spot */
    {  0.0f,  4.0f, 0.0f,  30 },   /* and over the curb while airborne */
    {  0.0f, -4.0f, 0.0f, 200 },   /* back, and on into the overhang */
    {  0.0f,  0.0f, 0.0f,  30 },   /* settle */
};

#define SELF_PHASE_COUNT ((int)(sizeof SELF_PHASES / sizeof SELF_PHASES[0]))

/* The world's fields, without the enclosing braces, so both writers below
   put the same shape in the same place. */
static void write_world(FILE *f, const HoloWalkWorld *world) {
    fputs("    \"radius\": ", f);
    holo_json_float(f, world->radius);
    fputs(",\n    \"height\": ", f);
    holo_json_float(f, world->height);
    fputs(",\n    \"gravity\": ", f);
    holo_json_float(f, world->gravity);
    fputs(",\n    \"floor_y\": ", f);
    holo_json_float(f, world->floor_y);
    fprintf(f, ",\n    \"max_walls\": %d,\n", HOLO_MAX_WALLS);
    fputs("    \"walls\": [\n", f);
    for (int i = 0; i < world->wall_count; i++) {
        fputs("      { \"min\": ", f);
        wv3(f, world->walls[i].min);
        fputs(", \"max\": ", f);
        wv3(f, world->walls[i].max);
        fputs(i + 1 < world->wall_count ? " },\n" : " }\n", f);
    }
    fputs("    ]\n", f);
}

int holo_walk_write_selftest_json(const char *path) {
    HoloWalkWorld world;
    HoloWalker start = (HoloWalker){ 0 };
    selftest_world(&world);
    start.pos = hv3(0.0f, 0.0f, 0.0f);
    start.grounded = 1;

    FILE *f = fopen(path, "wb");
    if (!f) {
        return 0;
    }
    fputs("{\n", f);
    fprintf(f, "  \"format\": \"%s\",\n", HOLO_WALK_JSON_FORMAT);
    fputs("  \"synthetic\": 1,\n", f);
    fputs("  \"world\": {\n", f);
    write_world(f, &world);
    fputs("  },\n", f);
    fputs("  \"start\": { \"pos\": ", f);
    wv3(f, start.pos);
    fputs(", \"vel\": ", f);
    wv3(f, start.vel);
    fprintf(f, ", \"grounded\": %d },\n", start.grounded ? 1 : 0);

    fputs("  \"trace\": {\n", f);
    fputs("    \"dt\": ", f);
    holo_json_float(f, WALK_DT);
    fputs(",\n    \"columns\": [\"vx\", \"vz\", \"jump\", "
          "\"px\", \"py\", \"pz\", \"grounded\"],\n", f);
    fputs("    \"steps\": [\n", f);

    HoloWalker w = start;
    int total = 0;
    for (int p = 0; p < SELF_PHASE_COUNT; p++) {
        total += SELF_PHASES[p].steps;
    }
    int written = 0;
    for (int p = 0; p < SELF_PHASE_COUNT; p++) {
        for (int s = 0; s < SELF_PHASES[p].steps; s++) {
            float jump = (s == 0) ? SELF_PHASES[p].jump : 0.0f;
            w.vel.x = SELF_PHASES[p].vx;
            w.vel.z = SELF_PHASES[p].vz;
            if (jump != 0.0f) {
                w.vel.y = jump;
            }
            holo_walk_step(&w, &world, WALK_DT);
            fputs("      [", f);
            holo_json_float(f, SELF_PHASES[p].vx);
            fputs(", ", f);
            holo_json_float(f, SELF_PHASES[p].vz);
            fputs(", ", f);
            holo_json_float(f, jump);
            fputs(", ", f);
            holo_json_float(f, w.pos.x);
            fputs(", ", f);
            holo_json_float(f, w.pos.y);
            fputs(", ", f);
            holo_json_float(f, w.pos.z);
            fprintf(f, ", %d]%s", w.grounded ? 1 : 0,
                    ++written < total ? ",\n" : "\n");
        }
    }
    fputs("    ]\n  }\n}\n", f);
    return fclose(f) == 0;
}

int holo_walk_write_json(const char *path, const HoloWalkWorld *world,
                         const HoloWalker *walker) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        return 0;
    }

    fputs("{\n", f);
    fprintf(f, "  \"format\": \"%s\",\n", HOLO_WALK_JSON_FORMAT);

    fputs("  \"world\": {\n", f);
    write_world(f, world);
    fputs("  },\n", f);

    fputs("  \"start\": { \"pos\": ", f);
    wv3(f, walker->pos);
    fputs(", \"vel\": ", f);
    wv3(f, walker->vel);
    fprintf(f, ", \"grounded\": %d },\n", walker->grounded ? 1 : 0);

    /* The trace. Each row is
         [vx, vz, jump, px, py, pz, grounded]
       -- what was commanded, then what came out. A reader sets vel.x and
       vel.z from the row, applies the jump to vel.y if it is non-zero, takes
       one step, and must land on the same position. */
    fputs("  \"trace\": {\n", f);
    fputs("    \"dt\": ", f);
    holo_json_float(f, WALK_DT);
    fputs(",\n    \"columns\": [\"vx\", \"vz\", \"jump\", "
          "\"px\", \"py\", \"pz\", \"grounded\"],\n", f);
    fputs("    \"steps\": [\n", f);

    HoloWalker w = *walker;   /* a copy: the caller's walker is not stepped */
    int total = PHASE_COUNT * PHASE_STEPS;
    int written = 0;
    for (int p = 0; p < PHASE_COUNT; p++) {
        for (int s = 0; s < PHASE_STEPS; s++) {
            float jump = (s == 0) ? PHASES[p].jump : 0.0f;
            w.vel.x = PHASES[p].vx;
            w.vel.z = PHASES[p].vz;
            if (jump != 0.0f) {
                w.vel.y = jump;
            }
            holo_walk_step(&w, world, WALK_DT);

            fputs("      [", f);
            holo_json_float(f, PHASES[p].vx);
            fputs(", ", f);
            holo_json_float(f, PHASES[p].vz);
            fputs(", ", f);
            holo_json_float(f, jump);
            fputs(", ", f);
            holo_json_float(f, w.pos.x);
            fputs(", ", f);
            holo_json_float(f, w.pos.y);
            fputs(", ", f);
            holo_json_float(f, w.pos.z);
            fprintf(f, ", %d]%s", w.grounded ? 1 : 0,
                    ++written < total ? ",\n" : "\n");
        }
    }

    fputs("    ]\n  }\n}\n", f);
    return fclose(f) == 0;
}
