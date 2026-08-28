#ifndef HOLO_COLLISION_H
#define HOLO_COLLISION_H

/* Walking: the only physics a mirror maze needs.
 *
 * A walker is a vertical capsule reduced to what matters -- a horizontal
 * radius and a height -- moving through a floor plane and a set of
 * axis-aligned boxes (the walls). Movement resolves one axis at a time,
 * which is what makes walking into a wall at an angle slide along it
 * instead of sticking: the blocked axis clamps, the free axis keeps its
 * velocity. Boxes are expanded by the radius, so the walker is a point
 * against rounded rooms. There are no rigid bodies, no impulses, no
 * broadphase: rooms have a dozen walls, and Crystal Mirror Maze's player
 * needs exactly this and gravity.
 *
 * Pure arithmetic, host-tested; the fixed timestep drives it.
 */

#include "linalg.h"

#define HOLO_MAX_WALLS 16

typedef struct {
    HoloV3 min, max;
} HoloAabb;

typedef struct {
    HoloV3 pos;        /* the feet */
    HoloV3 vel;
    int    grounded;
} HoloWalker;

typedef struct {
    float radius;      /* horizontal capsule radius */
    float height;      /* feet to crown; eyes sit somewhat below */
    float gravity;     /* positive; pulls -y */
    float floor_y;
    HoloAabb walls[HOLO_MAX_WALLS];
    int wall_count;
} HoloWalkWorld;

/* One simulation step: apply gravity, move each axis, resolve against the
   walls and the floor, update grounded. vel.x/z are the caller's to set
   each step (walking is kinematic); vel.y belongs to gravity and jumps. */
void holo_walk_step(HoloWalker *w, const HoloWalkWorld *world, float dt);

#endif
