#ifndef HOLO_ORACLE_H
#define HOLO_ORACLE_H

/* Holding the GPU to the CPU reference tracer.
 *
 * holo_oracle_diff reads the frame most recently presented, renders the
 * same frame through cpu_trace.c, and compares. Call it from a display
 * after_frame callback, a few frames in. The two images are float twins,
 * not bit twins -- drivers reassociate math -- so the bar is a mean error
 * under 1/255 and under 0.75% of pixels off by more than 8/255 (razor
 * edges: silhouettes, and glass at the critical angle, where a grazing
 * ray resolves differently on each side).
 */

#include "cpu_trace.h"

typedef struct {
    double mean;          /* mean abs error, in 1/255 levels */
    int    max;           /* worst single channel, in 1/255 levels */
    double outlier_pct;   /* % of pixels with any channel off by > 8 */
    int    width, height;
} HoloOracleStats;

/* Returns 1 when the GPU frame matches the oracle within the bars above,
   0 when it does not or the backend cannot read pixels back. The camera
   must be built with the real framebuffer aspect, and spectral must say
   which path the GPU rendered, so the CPU renders the same one. */
int holo_oracle_diff(const HoloScene *scene, const HoloCamera *cam,
                     int spectral, HoloOracleStats *stats);

/* Write out what a tracer OUTSIDE this process needs to be held to the same
   oracle: the uniform block exactly as the shader receives it, and the CPU's
   frame encoded the way holo_oracle_diff() encodes it before comparing.

   This exists because the GL and Metal tracers cannot always be run where
   they are written -- tools/gldiff renders shaders/trace.glsl in a browser
   and compares against these two files, which is how the GLSL tracer is held
   to the oracle from a machine with no GL toolchain at all.

   Writes build/<name>_params.bin (the raw block) and build/<name>_ref.bin
   (two int32 of width and height, then width*height*3 encoded bytes), both
   in native byte order. gpu_scene is the block a game passes to the display,
   with its size. Returns 1, or 0 if either file cannot be written. */
int holo_oracle_dump(const HoloScene *scene, const HoloCamera *cam,
                     int spectral, const void *gpu_scene, int gpu_scene_size,
                     const char *name);

#endif
