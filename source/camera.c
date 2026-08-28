/* See camera.h. */
#include <math.h>
#include "camera.h"

HoloCamera holo_camera_make(HoloV3 pos, HoloV3 target, HoloV3 up_hint,
                            float fov_deg, float aspect) {
    HoloCamera cam;
    cam.pos = pos;
    cam.forward = hv3_norm(hv3_sub(target, pos));
    cam.right = hv3_norm(hv3_cross(cam.forward, up_hint));
    cam.up = hv3_cross(cam.right, cam.forward);   /* unit by construction */
    cam.tan_half_fov = tanf(fov_deg * 0.5f * 3.14159265358979f / 180.0f);
    cam.aspect = aspect;
    return cam;
}

HoloRay holo_camera_ray(const HoloCamera *cam, float u, float v) {
    /* (u,v) in [0,1) with v down -> (x,y) in [-1,1] with y up. */
    float x = (u * 2.0f - 1.0f) * cam->tan_half_fov * cam->aspect;
    float y = (1.0f - v * 2.0f) * cam->tan_half_fov;
    HoloV3 dir = hv3_add(cam->forward,
                 hv3_add(hv3_scale(cam->right, x), hv3_scale(cam->up, y)));
    return (HoloRay){ .origin = cam->pos, .dir = hv3_norm(dir) };
}
