#ifndef HOLO_SCENE_JSON_H
#define HOLO_SCENE_JSON_H

/* A scene, written out as data. Write-only, on purpose.
 *
 * hologram builds scenes in code and will keep doing so: a game's rooms are
 * C struct literals, and nothing at runtime reads a scene file. This writer
 * exists for the editor, which needs a scene it can open, and for the same
 * reason holo_oracle_dump() exists -- a tracer outside this process cannot
 * be held to the oracle without being told what the oracle rendered.
 *
 * There is deliberately no reader here. A parser in C99 would be a runtime
 * dependency, a new failure mode inside a shipped game, and a second way to
 * build a scene competing with the first. The editor reads this JSON in
 * JavaScript and emits C; the engine never reads it back.
 *
 * Floats are written at the shortest precision that reads back bit-exact
 * through strtof, so the file round-trips exactly and still diffs like a
 * file a person wrote: 0.3 stays "0.3".
 */

#include <stdio.h>
#include "camera.h"
#include "cpu_trace.h"

#define HOLO_SCENE_JSON_FORMAT "hologram/scene/1"

/* The shortest text that reads back bit-exact through strtof, written to f.
   Exported because walk_json.c writes floats to the same standard, and there
   is no reason for two answers to "how is a float spelled". A non-finite
   value has no JSON literal at all and is written as 0. */
void holo_json_float(FILE *f, float v);

/* Write scene and camera to path as JSON. spectral records which path the
   frame was traced through, so a reader renders the same one. The camera is
   written as its orthonormal basis rather than a pos/target pair, because
   the basis is what reaches the shader -- a target would have to be
   re-normalized on the way back in and would not land on the same floats.

   Returns 1, or 0 if the file cannot be written. */
int holo_scene_write_json(const char *path, const HoloScene *scene,
                          const HoloCamera *cam, int spectral);

#endif
