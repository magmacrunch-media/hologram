/* See collision.h. */
#include "collision.h"

/* Does the walker's vertical span overlap this wall's? Feet exactly at a
   wall's top do not: standing on the floor beside a low wall is not a
   collision with it. */
static int spans_overlap(const HoloWalker *w, float height,
                         const HoloAabb *b) {
    return w->pos.y < b->max.y && w->pos.y + height > b->min.y;
}

/* Move one horizontal axis and clamp it back out of any wall entered.
   axis: 0 for x, 2 for z. Walls are expanded by the radius, so this is a
   point test with the clamp landing the capsule flush against the face. */
static void move_axis(HoloWalker *w, const HoloWalkWorld *world,
                      float delta, int axis) {
    float *p = axis == 0 ? &w->pos.x : &w->pos.z;
    *p += delta;
    for (int i = 0; i < world->wall_count; i++) {
        const HoloAabb *b = &world->walls[i];
        if (!spans_overlap(w, world->height, b)) {
            continue;
        }
        float minx = b->min.x - world->radius, maxx = b->max.x + world->radius;
        float minz = b->min.z - world->radius, maxz = b->max.z + world->radius;
        if (w->pos.x <= minx || w->pos.x >= maxx ||
            w->pos.z <= minz || w->pos.z >= maxz) {
            continue;
        }
        /* Inside: back out along the axis that just moved, on the side the
           movement came from. */
        if (axis == 0) {
            w->pos.x = delta > 0.0f ? minx : maxx;
        } else {
            w->pos.z = delta > 0.0f ? minz : maxz;
        }
    }
}

void holo_walk_step(HoloWalker *w, const HoloWalkWorld *world, float dt) {
    move_axis(w, world, w->vel.x * dt, 0);
    move_axis(w, world, w->vel.z * dt, 2);

    w->vel.y -= world->gravity * dt;
    w->pos.y += w->vel.y * dt;
    if (w->pos.y <= world->floor_y) {
        w->pos.y = world->floor_y;
        w->vel.y = 0.0f;
        w->grounded = 1;
    } else {
        w->grounded = 0;
    }
}
