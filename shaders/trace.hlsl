/* The tracer, ported statement for statement from cpu_trace.c -- same
 * intersections, same Lambert, same mirror walk, same sky, same checker.
 * The two must stay twins: the oracle diff holds this file to the CPU's
 * pixels, and a change that lands in one and not the other is the diff's
 * job to catch.
 *
 * Uniform layout matches HoloGpuScene in source/gpu_scene.h field for
 * field; both sides count in float4s, so keep every float3 padded.
 */

cbuffer params : register(b0) {
    float2 res; float time; float pad0;          /* HoloDisplayUniforms */
    float3 cam_pos;   float tan_half_fov;
    float3 cam_fwd;   float pad_a;
    float3 cam_right; float sphere_count;
    float3 cam_up;    float has_floor;
    float3 sun_dir;   float floor_y;
    float3 horizon;   float rect_count;
    float3 zenith;    float floor_mirror;
    float3 floor_a;   float pad_b;
    float3 floor_b;   float pad_c;
    float4 sph_center_radius[8];
    float4 sph_albedo_mirror[8];
    float4 rect_corner_mirror[8];
    float4 rect_edge_u[8];
    float4 rect_edge_v[8];
    float4 rect_albedo[8];
};

static const float T_MIN = 1e-3;      /* HOLO_T_MIN */
static const float AMBIENT = 0.1;     /* HOLO_AMBIENT */
static const int   MAX_BOUNCE = 16;   /* HOLO_MAX_BOUNCE */

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

/* holo_ray_rect: the plane first, then Gram-solved affine coordinates. */
bool ray_rect(float3 ro, float3 rd, float3 corner, float3 eu, float3 ev,
              out float t, out float3 normal) {
    t = 0;
    normal = normalize(cross(eu, ev));
    float denom = dot(normal, rd);
    if (abs(denom) < 1e-8) return false;
    t = dot(corner - ro, normal) / denom;
    if (t <= T_MIN) return false;
    float3 rel = ro + t * rd - corner;
    float uu = dot(eu, eu), vv = dot(ev, ev), uv = dot(eu, ev);
    float ru = dot(rel, eu), rv = dot(rel, ev);
    float det = uu * vv - uv * uv;
    float u = (ru * vv - rv * uv) / det;
    float v = (rv * uu - ru * uv) / det;
    if (u < 0.0 || u > 1.0 || v < 0.0 || v > 1.0) return false;
    if (denom > 0.0) normal = -normal;
    return true;
}

/* holo_ray_plane, for the y-up floor only (all any scene has). */
bool ray_floor(float3 ro, float3 rd, out float t) {
    t = 0;
    if (abs(rd.y) < 1e-8) return false;
    t = (floor_y - ro.y) / rd.y;
    return t > T_MIN;
}

/* nearest_hit: where, which way it faces, what it is made of. */
bool nearest_hit(float3 ro, float3 rd,
                 out float best_t, out float3 best_n,
                 out float3 albedo, out float mirror) {
    bool found = false;
    best_t = 1e30; best_n = float3(0, 0, 0);
    albedo = float3(0, 0, 0); mirror = 0;

    float t; float3 n;
    for (int i = 0; i < (int)sphere_count; i++) {
        if (ray_sphere(ro, rd, sph_center_radius[i].xyz,
                       sph_center_radius[i].w, t, n) && t < best_t) {
            best_t = t; best_n = n;
            albedo = sph_albedo_mirror[i].xyz;
            mirror = sph_albedo_mirror[i].w;
            found = true;
        }
    }
    for (int j = 0; j < (int)rect_count; j++) {
        if (ray_rect(ro, rd, rect_corner_mirror[j].xyz,
                     rect_edge_u[j].xyz, rect_edge_v[j].xyz, t, n) &&
            t < best_t) {
            best_t = t; best_n = n;
            albedo = rect_albedo[j].xyz;
            mirror = rect_corner_mirror[j].w;
            found = true;
        }
    }
    if (has_floor > 0.5 && ray_floor(ro, rd, t) && t < best_t) {
        best_t = t;
        best_n = float3(0, rd.y < 0.0 ? 1 : -1, 0);
        float3 p = ro + t * rd;
        int cell = (int)floor(p.x) + (int)floor(p.z);
        albedo = (cell & 1) ? floor_b : floor_a;
        mirror = floor_mirror;
        found = true;
    }
    return found;
}

/* sun_blocked: any sphere or panel between the point and the sun. */
bool sun_blocked(float3 p) {
    float t; float3 n;
    for (int i = 0; i < (int)sphere_count; i++) {
        if (ray_sphere(p, sun_dir, sph_center_radius[i].xyz,
                       sph_center_radius[i].w, t, n)) {
            return true;
        }
    }
    for (int j = 0; j < (int)rect_count; j++) {
        if (ray_rect(p, sun_dir, rect_corner_mirror[j].xyz,
                     rect_edge_u[j].xyz, rect_edge_v[j].xyz, t, n)) {
            return true;
        }
    }
    return false;
}

float3 sky(float3 dir) {
    return lerp(horizon, zenith, 0.5 * (dir.y + 1.0));
}

/* holo_trace_ray: the mirror walk. */
float3 trace(float3 ro, float3 rd) {
    float3 color = float3(0, 0, 0);
    float3 throughput = float3(1, 1, 1);

    for (int bounce = 0; bounce <= MAX_BOUNCE; bounce++) {
        float best_t; float3 best_n; float3 albedo; float mirror;
        if (!nearest_hit(ro, rd, best_t, best_n, albedo, mirror)) {
            color += throughput * sky(rd);
            break;
        }
        float3 p = ro + best_t * rd;

        float diffuse = max(dot(best_n, sun_dir), 0.0);
        if (diffuse > 0.0 && sun_blocked(p)) diffuse = 0.0;
        float3 matte = albedo * (AMBIENT + (1.0 - AMBIENT) * diffuse);
        color += throughput * matte * (1.0 - mirror);
        if (mirror <= 0.0) break;

        throughput *= albedo * mirror;
        ro = p;
        rd = reflect(rd, best_n);
    }
    return color;
}

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    /* holo_camera_ray, with the aspect derived from res on both sides. */
    float x = (uv.x * 2.0 - 1.0) * tan_half_fov * (res.x / res.y);
    float y = (1.0 - uv.y * 2.0) * tan_half_fov;
    float3 rd = normalize(cam_fwd + cam_right * x + cam_up * y);
    return float4(trace(cam_pos, rd), 1.0);
}
