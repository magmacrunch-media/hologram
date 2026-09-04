#ifndef HOLO_PICK_JSON_H
#define HOLO_PICK_JSON_H

/* Which primitive each ray lands on, as data. Write-only, like the other
 * two writers here.
 *
 * WHAT THIS IS FOR
 *
 * Clicking a thing in a rendered image means asking which primitive a pixel
 * belongs to, and the tracer does not report that -- it returns a colour.
 * Anything outside this process that wants to answer it has to intersect the
 * scene itself, which is a second copy of the ray-versus-surface code, and a
 * second copy nothing checks is what this repo spends its effort not having.
 *
 * So the same arrangement as everywhere else: the engine writes down what it
 * hits, and the copy is replayed against it. This dumps a grid of rays
 * through the camera and, for each, the primitive nearest along it. A reader
 * casts the same grid through its own intersections and must land on the
 * same primitives.
 *
 * The traversal below mirrors cpu_trace.c's nearest_hit exactly, including
 * its order -- spheres, then rects, then dishes, then the floor -- and its
 * strict `t < best`, which on an exact tie keeps whichever was found first.
 * That ordering is not arbitrary decoration: it decides the answer wherever
 * two surfaces meet, and a reader that walks them in another order disagrees
 * along every shared edge.
 *
 * It records identity, not distance. What a picker owes is the right object,
 * and a t that differs in its last bits is not a wrong answer; a ray that
 * lands on a different primitive is.
 */

#include "camera.h"
#include "cpu_trace.h"

#define HOLO_PICK_JSON_FORMAT "hologram/pick/1"

/* What a ray found. */
#define HOLO_PICK_NONE   0
#define HOLO_PICK_SPHERE 1
#define HOLO_PICK_RECT   2
#define HOLO_PICK_DISH   3
#define HOLO_PICK_FLOOR  4

/* The grid is in camera uv rather than pixels, so the file does not depend
   on the window the dump happened to run in. */
#define HOLO_PICK_COLS 64
#define HOLO_PICK_ROWS 48

/* Nearest primitive along `ray`. Returns one of the HOLO_PICK_* kinds and
   sets *index to the primitive's index (-1 for none and for the floor).
   Exposed because it is the whole of what a picker does, and a test or a
   tool may want it without a file. */
int holo_pick_ray(const HoloScene *scene, HoloRay ray, int *index);

/* Write the grid to path. Returns 1, or 0 if the file cannot be written. */
int holo_pick_write_json(const char *path, const HoloScene *scene,
                         const HoloCamera *cam);

#endif
