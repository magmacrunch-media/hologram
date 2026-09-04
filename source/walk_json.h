#ifndef HOLO_WALK_JSON_H
#define HOLO_WALK_JSON_H

/* The walk world as data, and a trace of it being walked. Write-only, like
 * scene_json.h and for the same reasons.
 *
 * A HoloWalkWorld is not part of a HoloScene -- the walls are hand-authored
 * boxes that deliberately differ from the panels you can see, thicker and
 * set back, because what stops a player and what reflects light are not the
 * same surface. So a dumped scene says nothing about where you can stand,
 * and anything outside the engine that wants to walk a room has to be told
 * separately. This tells it.
 *
 * WHY A TRACE COMES WITH IT
 *
 * Anything that walks the room outside this process is a second copy of
 * holo_walk_step, and a second copy nothing checks is exactly the kind of
 * thing the oracle exists to prevent. The other copies in this repo are all
 * held to something: the shaders to cpu_trace.c by the oracle diff, the
 * shader's slot map to HoloGpuScene by test_gpu_layout, the editor's packer
 * to build/<name>_params.bin. This is that check for the walk.
 *
 * The writer runs a scripted sequence of steps against a copy of the walker
 * and records, for every step, the horizontal velocity it commanded, whether
 * it jumped, and the position and grounded flag that came out. A reader
 * replays the recorded commands through its own walk step and compares
 * positions. The script is not shared knowledge -- it is in the file -- so
 * there is nothing to keep in sync but the arithmetic itself, which is the
 * thing under test.
 *
 * The script walks hard into each horizontal direction in turn, then
 * diagonally (the case that must slide along a wall rather than stick), then
 * jumps and falls. In any room-shaped world that meets walls, the floor, and
 * the span test that lets you stand beside a low box without catching on it.
 */

#include "collision.h"

#define HOLO_WALK_JSON_FORMAT "hologram/walk/1"

/* Write world, the walker's starting state, and the trace to path. `walker`
   is copied, not stepped, so calling this leaves the caller's walker alone.
   Returns 1, or 0 if the file cannot be written. */
int holo_walk_write_json(const char *path, const HoloWalkWorld *world,
                         const HoloWalker *walker);

/* The same, for a world built here rather than dumped from a game.
 *
 * A room does not exercise everything a walk step does. Every wall in one
 * starts at the floor, so the vertical span test never changes its answer,
 * and a scripted walk only meets the walls it happens to pass -- a reader
 * whose span test is wrong walks through an overhang and a room trace never
 * notices. This world is three walls chosen to be awkward: a plain one, a
 * curb low enough to jump over, and an overhang that clears the floor by
 * less than a walker's height. Independent of any example, so every one of
 * them writes the same file. Returns 1, or 0 if it cannot be written. */
int holo_walk_write_selftest_json(const char *path);

#endif
