#ifndef HOLO_CAMERA_H
#define HOLO_CAMERA_H

/* The camera is a ray generator: give it a pixel, get the ray that pixel
 * sees along. Nothing here draws -- the CPU oracle and the GPU shader both
 * generate rays this way, which is what makes their images comparable.
 */

#include "geometry.h"

typedef struct {
    HoloV3 pos;
    HoloV3 forward, right, up;   /* orthonormal basis, right-handed */
    float  tan_half_fov;         /* vertical */
    float  aspect;               /* width / height */
} HoloCamera;

/* A pinhole camera at pos looking at target. fov_deg is the vertical field
   of view; up_hint just breaks the roll ambiguity (hv3(0,1,0) is the usual
   answer) and need not be orthogonal to the view. */
HoloCamera holo_camera_make(HoloV3 pos, HoloV3 target, HoloV3 up_hint,
                            float fov_deg, float aspect);

/* The ray through (u, v) on the image, both in [0,1): u runs left to right,
   v top to bottom, matching the display's uv. Sample pixel centers as
   ((x + 0.5) / w, (y + 0.5) / h). */
HoloRay holo_camera_ray(const HoloCamera *cam, float u, float v);

#endif
