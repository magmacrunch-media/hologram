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
#include "spectrum.h"

typedef struct {
    HoloDisplayUniforms display;           /* filled by display.c each frame */

    float cam_pos[3];   float tan_half_fov;
    float cam_fwd[3];   float spectral;    /* 1: trace per wavelength */
    float cam_right[3]; float sphere_count;
    float cam_up[3];    float has_floor;
    float sun_dir[3];   float floor_y;
    float horizon[3];   float rect_count;
    float zenith[3];    float floor_mirror;
    float floor_a[3];   float sun_disk_cos;
    float floor_b[3];   float sun_disk_intensity;

    float sph_center_radius[HOLO_MAX_SPHERES][4];   /* xyz center, w radius */
    float sph_albedo_mirror[HOLO_MAX_SPHERES][4];   /* xyz albedo, w mirror */
    float sph_glass[HOLO_MAX_SPHERES][4];           /* x transmit, y ior, z disperse */
    float rect_corner_mirror[HOLO_MAX_RECTS][4];    /* xyz corner, w mirror */
    float rect_edge_u[HOLO_MAX_RECTS][4];
    float rect_edge_v[HOLO_MAX_RECTS][4];
    float rect_albedo[HOLO_MAX_RECTS][4];
    float rect_glass[HOLO_MAX_RECTS][4];            /* x transmit, y ior, z disperse, w retard */
    /* x mode (0 none / 1 polarizer / 2 waveplate / 3 grating), yzw the
       filter axis or the groove direction. A grating rect repurposes the
       glass and albedo lanes it never uses -- rect_glass becomes (period,
       w-1, w0, w+1) and rect_albedo.x the +2 weight -- because the shader
       cannot afford two more dynamically indexed arrays: fxc's indexable
       register file tops out and silently aliases the overflow into other
       arrays. Pack, don't append. */
    float rect_filter[HOLO_MAX_RECTS][4];
    float dish_apex_r[HOLO_MAX_DISHES][4];          /* xyz apex, w curv_r */
    float dish_axis_k[HOLO_MAX_DISHES][4];          /* xyz axis, w conic_k */
    float dish_albedo_mirror[HOLO_MAX_DISHES][4];   /* xyz albedo, w mirror */
    float dish_rim_count[HOLO_MAX_DISHES][4];       /* x rim; [0].y = dish count */

    /* x = lambda in um, yzw = that sample's CIE-derived sRGB weight. The
       shader must not re-derive these: CPU and GPU folding the same floats
       is part of what the oracle diff certifies. */
    float spectral_lw[HOLO_WAVELENGTHS][4];

    /* Up to two gratings, as SCALAR (non-array) fields the shader reads
       statically. Every attempt to read per-grating data through a
       dynamically indexed cbuffer array came back as garbage under fxc --
       across shader models and restructurings -- so the grating branch
       selects one of these two slots by comparing the hit's rect index
       against the slot's own index. More than two gratings render as
       matte black on the GPU (the CPU has no such limit). */
    float grat0_groove_idx[4];   /* xyz groove dir, w rect index or -1 */
    float grat0_period_w[4];     /* x period um, yzw weights m=-1,0,+1 */
    float grat1_groove_idx[4];
    float grat1_period_w[4];
    float grat_w2[4];            /* x slot0's +2 weight, y slot1's */
} HoloGpuScene;

/* Write scene and camera into the block. The camera's aspect is NOT carried:
   the shader derives it from the framebuffer size in its uniforms, so the
   image stays right when the window is resized. spectral chooses the
   shader's path: 0 traces RGB, 1 traces per wavelength. */
void holo_gpu_scene_fill(HoloGpuScene *gpu, const HoloScene *scene,
                         const HoloCamera *cam, int spectral);

#endif
