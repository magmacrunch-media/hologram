/* Walking, held to what a player's hands would notice within seconds:
 * falling stops at the floor, walls stop you a radius away, walking into
 * a wall at an angle slides along it at full sideways speed, and a jump
 * comes back down. All arithmetic, so all of it lives here rather than in
 * a playtest.
 *
 *   build.bat test
 */
#include <stdio.h>
#include "harness.h"
#include "collision.h"

static HoloWalkWorld room(void) {
    HoloWalkWorld world = {
        .radius = 0.3f,
        .height = 1.7f,
        .gravity = 20.0f,
        .floor_y = 0.0f,
        /* One wall slab along x at z = 2..2.5, full height. */
        .walls = { { .min = { -5, 0, 2.0f }, .max = { 5, 3, 2.5f } } },
        .wall_count = 1,
    };
    return world;
}

/* Step until dt seconds have passed, in fixed slices like the game. */
static void run(HoloWalker *w, const HoloWalkWorld *world, float seconds) {
    int steps = (int)(seconds / 0.005f);
    for (int i = 0; i < steps; i++) {
        holo_walk_step(w, world, 0.005f);
    }
}

static void test_gravity(void) {
    printf("collision: falling ends at the floor\n");
    HoloWalkWorld world = room();
    HoloWalker w = { .pos = hv3(0, 2, 0) };
    run(&w, &world, 2.0f);
    check_close(w.pos.y, 0.0f, "landed");
    check_int(w.grounded, 1, "and knows it");
    check_close(w.vel.y, 0.0f, "vertical speed spent");
}

static void test_wall_stops(void) {
    printf("collision: a wall stops you a radius away\n");
    HoloWalkWorld world = room();
    HoloWalker w = { .pos = hv3(0, 0, 0), .grounded = 1 };
    w.vel = hv3(0, 0, 2.0f);   /* straight at the wall */
    run(&w, &world, 3.0f);
    check_close(w.pos.z, 2.0f - 0.3f, "flush against the face");
    check_close(w.pos.x, 0.0f, "no sideways drift");
}

static void test_slide(void) {
    printf("collision: walking into a wall at an angle slides\n");
    HoloWalkWorld world = room();
    HoloWalker w = { .pos = hv3(0, 0, 1.5f), .grounded = 1 };
    w.vel = hv3(1.0f, 0, 1.0f);   /* diagonally into the wall */
    run(&w, &world, 2.0f);
    check_close(w.pos.z, 1.7f, "z pinned at the wall");
    check_close(w.pos.x, 2.0f, "x kept its full 1 m/s for 2 s");
}

static void test_wall_top(void) {
    printf("collision: above the wall is past the wall\n");
    HoloWalkWorld world = room();
    /* Flying over the slab (walls end at y = 3): no collision. */
    HoloWalker w = { .pos = hv3(0, 4, 0) };
    w.vel = hv3(0, 0, 4.0f);
    for (int i = 0; i < 160; i++) {
        holo_walk_step(&w, &world, 0.005f);
        w.vel.y = 0;   /* hold altitude; this test is about the span check */
    }
    check(w.pos.z > 2.5f, "sailed over the slab");
}

static void test_jump(void) {
    printf("collision: a jump comes back down\n");
    HoloWalkWorld world = room();
    HoloWalker w = { .pos = hv3(0, 0, 0), .grounded = 1 };
    w.vel.y = 6.0f;
    holo_walk_step(&w, &world, 0.005f);
    check_int(w.grounded, 0, "airborne");
    check(w.pos.y > 0.0f, "off the floor");
    run(&w, &world, 2.0f);
    check_int(w.grounded, 1, "down again");
    check_close(w.pos.y, 0.0f, "on the floor");
}

int main(void) {
    test_gravity();
    test_wall_stops();
    test_slide();
    test_wall_top();
    test_jump();
    return report();
}
