/* The M1 tracer, ported statement for statement from cpu_trace.c -- same
 * intersections, same Lambert, same sky, same checker. The two must stay
 * twins: m2_gpu --diff holds this file to the CPU oracle's pixels, and a
 * change that lands in one and not the other is the diff's job to catch.
 *
 * Uniform layout matches M2Uniforms in examples/m2_gpu/main.c field for
 * field; both sides count in float4s, so keep every float3 padded.
 */

cbuffer params : register(b0) {
    float2 res; float time; float pad0;          /* HoloDisplayUniforms */
    float3 cam_pos;   float tan_half_fov;
    float3 cam_fwd;   float pad_a;
    float3 cam_right; float sphere_count;
    float3 cam_up;    float has_floor;
    float3 sun_dir;   float floor_y;
    float3 horizon;   float pad1;
    float3 zenith;    float pad2;
    float3 floor_a;   float pad3;
    float3 floor_b;   float pad4;
    float4 sph_center_radius[8];
    float4 sph_albedo[8];
};

static const float T_MIN = 1e-3;      /* HOLO_T_MIN */
static const float AMBIENT = 0.1;     /* HOLO_AMBIENT */

/* holo_ray_sphere. Writes t and the normal on the arriving side. */
bool ray_sphere(float3 ro, float3 rd, float3 center, float radius,
                out float t, out float3 normal) {
    t = 0; normal = float3(0, 0, 0);
    float3 oc = ro - center;
    float b = dot(oc, rd);
    float c = dot(oc, oc) - radius * radius;
    float disc = b * b - c;
    if (disc < 0.0) return false;
    float sq = sqrt(disc);
    t = -b - sq;
    if (t <= T_MIN) t = -b + sq;
    if (t <= T_MIN) return false;
    float3 n = (ro + t * rd - center) / radius;
    normal = dot(n, rd) < 0.0 ? n : -n;
    return true;
}

/* holo_ray_plane, for the y-up floor only (all this scene has). */
bool ray_floor(float3 ro, float3 rd, out float t) {
    t = 0;
    if (abs(rd.y) < 1e-8) return false;
    t = (floor_y - ro.y) / rd.y;
    return t > T_MIN;
}

/* sun_blocked: any sphere between the point and the sun. */
bool sun_blocked(float3 p) {
    float t; float3 n;
    for (int i = 0; i < (int)sphere_count; i++) {
        if (ray_sphere(p, sun_dir, sph_center_radius[i].xyz,
                       sph_center_radius[i].w, t, n)) {
            return true;
        }
    }
    return false;
}

float3 sky(float3 dir) {
    return lerp(horizon, zenith, 0.5 * (dir.y + 1.0));
}

/* holo_trace_ray. */
float3 trace(float3 ro, float3 rd) {
    float best_t = 1e30;
    float3 best_n = float3(0, 0, 0);
    int what = -1;                      /* -1 nothing, -2 floor, else sphere */

    float t; float3 n;
    for (int i = 0; i < (int)sphere_count; i++) {
        if (ray_sphere(ro, rd, sph_center_radius[i].xyz,
                       sph_center_radius[i].w, t, n) && t < best_t) {
            best_t = t; best_n = n; what = i;
        }
    }
    if (has_floor > 0.5 && ray_floor(ro, rd, t) && t < best_t) {
        best_t = t;
        best_n = float3(0, rd.y < 0.0 ? 1 : -1, 0);
        what = -2;
    }
    if (what == -1) return sky(rd);

    float3 p = ro + best_t * rd;
    float3 albedo;
    if (what == -2) {
        int cell = (int)floor(p.x) + (int)floor(p.z);
        albedo = (cell & 1) ? floor_b : floor_a;
    } else {
        albedo = sph_albedo[what].xyz;
    }

    float diffuse = max(dot(best_n, sun_dir), 0.0);
    if (sun_blocked(p)) diffuse = 0.0;
    return albedo * (AMBIENT + (1.0 - AMBIENT) * diffuse);
}

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    /* holo_camera_ray, with the aspect derived from res on both sides. */
    float x = (uv.x * 2.0 - 1.0) * tan_half_fov * (res.x / res.y);
    float y = (1.0 - uv.y * 2.0) * tan_half_fov;
    float3 rd = normalize(cam_fwd + cam_right * x + cam_up * y);
    return float4(trace(cam_pos, rd), 1.0);
}
