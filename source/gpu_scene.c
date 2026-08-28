/* See gpu_scene.h. */
#include "gpu_scene.h"

/* Every vector field in the block is three floats with a scalar riding in
   the fourth lane; put3 writes the three and leaves the lane alone. */
static void put3(float *dst, HoloV3 v) {
    dst[0] = v.x;
    dst[1] = v.y;
    dst[2] = v.z;
}

void holo_gpu_scene_fill(HoloGpuScene *gpu, const HoloScene *scene,
                         const HoloCamera *cam, int spectral) {
    put3(gpu->cam_pos, cam->pos);
    gpu->tan_half_fov = cam->tan_half_fov;
    put3(gpu->cam_fwd, cam->forward);
    gpu->spectral = spectral ? 1.0f : 0.0f;
    put3(gpu->cam_right, cam->right);
    put3(gpu->cam_up, cam->up);

    gpu->sphere_count = (float)scene->sphere_count;
    gpu->rect_count = (float)scene->rect_count;
    gpu->has_floor = scene->has_floor ? 1.0f : 0.0f;
    gpu->floor_y = scene->floor_y;
    gpu->floor_mirror = scene->floor_mirror;

    put3(gpu->sun_dir, scene->sun_dir);
    put3(gpu->horizon, scene->horizon);
    put3(gpu->zenith, scene->zenith);
    put3(gpu->floor_a, scene->floor_a);
    put3(gpu->floor_b, scene->floor_b);

    for (int i = 0; i < scene->sphere_count; i++) {
        put3(gpu->sph_center_radius[i], scene->spheres[i].center);
        gpu->sph_center_radius[i][3] = scene->spheres[i].radius;
        put3(gpu->sph_albedo_mirror[i], scene->spheres[i].albedo);
        gpu->sph_albedo_mirror[i][3] = scene->spheres[i].mirror;
        gpu->sph_glass[i][0] = scene->spheres[i].transmit;
        gpu->sph_glass[i][1] = scene->spheres[i].ior;
        gpu->sph_glass[i][2] = scene->spheres[i].disperse;
    }
    for (int i = 0; i < scene->rect_count; i++) {
        put3(gpu->rect_corner_mirror[i], scene->rects[i].corner);
        gpu->rect_corner_mirror[i][3] = scene->rects[i].mirror;
        put3(gpu->rect_edge_u[i], scene->rects[i].edge_u);
        put3(gpu->rect_edge_v[i], scene->rects[i].edge_v);
        put3(gpu->rect_albedo[i], scene->rects[i].albedo);
        gpu->rect_glass[i][0] = scene->rects[i].transmit;
        gpu->rect_glass[i][1] = scene->rects[i].ior;
        gpu->rect_glass[i][2] = scene->rects[i].disperse;
    }

    for (int i = 0; i < HOLO_WAVELENGTHS; i++) {
        HoloV3 w = holo_spectral_weight(i);
        gpu->spectral_lw[i][0] = holo_lambda(i);
        gpu->spectral_lw[i][1] = w.x;
        gpu->spectral_lw[i][2] = w.y;
        gpu->spectral_lw[i][3] = w.z;
    }
}
