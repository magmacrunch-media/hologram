#ifndef HOLO_ORACLE_H
#define HOLO_ORACLE_H

/* Holding the GPU to the CPU reference tracer.
 *
 * holo_oracle_diff reads the frame most recently presented, renders the
 * same frame through cpu_trace.c, and compares. Call it from a display
 * after_frame callback, a few frames in. The two images are float twins,
 * not bit twins -- drivers reassociate math -- so the bar is a mean error
 * under 1/255 and under 0.5% of pixels off by more than 8/255 (silhouette
 * pixels, where a grazing ray hits on one side and misses on the other).
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

#endif
