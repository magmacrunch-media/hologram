/* Round-trip check for editor/core/emit.js's worldToC, the companion to
 * roundtrip.c.
 *
 * The scene emitter is checked by packing what it emits and comparing the
 * uniform block against the one --dump wrote. A walk world never reaches a
 * uniform block, so it is checked the other way round: compile the emitted
 * literal, hand it to holo_walk_write_json, and compare the "world" object
 * of the result against the same object in build/m7_room_walk.json. If the
 * emitter dropped a field, swapped a min for a max, or lost a wall, those
 * two objects stop matching.
 *
 * The literal below was copied verbatim out of the editor's "show C" panel,
 * having opened m7_room and changed nothing.
 *
 * Not part of build.bat test: it needs a dump, which needs a GPU and a
 * window, and the host tests deliberately need neither. Run it by hand after
 * changing worldToC, regenerating the literal from the panel first:
 *
 *     build.bat
 *     build\m7_room.exe --dump
 *     cl /nologo /std:c11 /W4 /Isource /Febuild\worldtrip.exe ^
 *         editor/worldtrip.c source/linalg.c source/polar.c ^
 *         source/geometry.c source/camera.c source/collision.c ^
 *         source/cpu_trace.c source/gpu_scene.c source/scene_json.c ^
 *         source/spectrum.c source/timestep.c source/walk_json.c
 *     build\worldtrip.exe
 *
 * then compare the two "world" objects -- the program prints the path it
 * wrote and the exit code is 0 when it wrote it.
 */
#include <stdio.h>
#include <string.h>
#include "../source/walk_json.h"

int main(void) {
    HoloWalkWorld world;
    HoloWalker start;
    memset(&world, 0, sizeof world);
    memset(&start, 0, sizeof start);

    /* ---- BEGIN emitted by the editor ---- */
    world = (HoloWalkWorld){
        .radius = 0.3f,
        .height = 1.7f,
        .gravity = 20.0f,
        .walls = {
            { .min = { -4.3f, 0.0f, -6.3f }, .max = { -4.0f, 3.0f, 6.3f } },
            { .min = { 4.0f, 0.0f, -6.3f }, .max = { 4.3f, 3.0f, 6.3f } },
            { .min = { -4.3f, 0.0f, -6.3f }, .max = { 4.3f, 3.0f, -6.0f } },
            { .min = { -4.3f, 0.0f, 6.0f }, .max = { 4.3f, 3.0f, 6.3f } },
            { .min = { 0.8f, 0.0f, 0.55f }, .max = { 2.8f, 2.4f, 0.65f } },
        },
        .wall_count = 5,
    };
    /* ---- END emitted by the editor ---- */

    /* m7_room's walker starts here; the start block is not what is being
       checked, but the file wants one. */
    start.pos = hv3(0.0f, 0.0f, 4.5f);
    start.grounded = 1;

    const char *path = "build/worldtrip_walk.json";
    if (!holo_walk_write_json(path, &world, &start)) {
        printf("could not write %s\n", path);
        return 2;
    }
    printf("wrote %s -- compare its \"world\" with build/m7_room_walk.json\n",
           path);
    return 0;
}
