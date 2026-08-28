#ifndef HOLO_GPU_SCENE_H
#define HOLO_GPU_SCENE_H

/* The scene as the shader sees it: one uniform block, float4 by float4.
 *
 * This struct and the cbuffer in shaders/trace.hlsl are the same layout
 * written in two languages, and they must change together -- the oracle
 * diff is what catches them drifting apart. Fields pack HLSL-style: every
 * float3 is followed by a float that rides in its fourth lane.
 */

#include "cpu_trace.h"
#include "display.h"

typedef struct {
    HoloDisplayUniforms display;           /* filled by display.c each frame */

    float cam_pos[3];   float tan_half_fov;
    float cam_fwd[3];   float pad_a;
    float cam_right[3]; float sphere_count;
    float cam_up[3];    float has_floor;
    float sun_dir[3];   float floor_y;
    float horizon[3];   float rect_count;
    float zenith[3];    float floor_mirror;
    float floor_a[3];   float pad_b;
    float floor_b[3];   float pad_c;

    float sph_center_radius[HOLO_MAX_SPHERES][4];   /* xyz center, w radius */
    float sph_albedo_mirror[HOLO_MAX_SPHERES][4];   /* xyz albedo, w mirror */
    float sph_glass[HOLO_MAX_SPHERES][4];           /* x transmit, y ior */
    float rect_corner_mirror[HOLO_MAX_RECTS][4];    /* xyz corner, w mirror */
    float rect_edge_u[HOLO_MAX_RECTS][4];
    float rect_edge_v[HOLO_MAX_RECTS][4];
    float rect_albedo[HOLO_MAX_RECTS][4];
    float rect_glass[HOLO_MAX_RECTS][4];            /* x transmit, y ior */
} HoloGpuScene;

/* Write scene and camera into the block. The camera's aspect is NOT carried:
   the shader derives it from the framebuffer size in its uniforms, so the
   image stays right when the window is resized. */
void holo_gpu_scene_fill(HoloGpuScene *gpu, const HoloScene *scene,
                         const HoloCamera *cam);

#endif
