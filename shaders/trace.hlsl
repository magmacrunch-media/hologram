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
    float3 cam_fwd;   float spectral;
    float3 cam_right; float sphere_count;
    float3 cam_up;    float has_floor;
    float3 sun_dir;   float floor_y;
    float3 horizon;   float rect_count;
    float3 zenith;    float floor_mirror;
    float3 floor_a;   float pad_b;
    float3 floor_b;   float pad_c;
    float4 sph_center_radius[8];
    float4 sph_albedo_mirror[8];
    float4 sph_glass[8];
    float4 rect_corner_mirror[8];
    float4 rect_edge_u[8];
    float4 rect_edge_v[8];
    float4 rect_albedo[8];
    float4 rect_glass[8];
    float4 spectral_lw[12];   /* x lambda um, yzw CIE-derived sRGB weight */
};

static const float T_MIN = 1e-3;      /* HOLO_T_MIN */
static const float AMBIENT = 0.1;     /* HOLO_AMBIENT */
static const int   MAX_BOUNCE = 16;   /* HOLO_MAX_BOUNCE */
static const int   MAX_RAYS = 32;     /* HOLO_MAX_RAYS */
static const int   STACK = 16;        /* HOLO_STACK */
static const float MIN_TP = 0.002;    /* HOLO_MIN_TP */
static const int   WAVELENGTHS = 12;  /* HOLO_WAVELENGTHS */

/* holo_albedo_at: an RGB color read at one wavelength through three smooth
   bands that partition unity -- neutral colors are exact. */
float albedo_at(float3 rgb, float lambda_um) {
    float t_bg = saturate((lambda_um - 0.475) / (0.510 - 0.475));
    float t_gr = saturate((lambda_um - 0.565) / (0.610 - 0.565));
    return rgb.r * t_gr + rgb.g * (t_bg - t_gr) + rgb.b * (1.0 - t_bg);
}

/* holo_ior_at: Cauchy dispersion around the sodium D line. */
float ior_at(float ior_d, float cauchy_b, float lambda_um) {
    const float inv_d2 = 1.0 / (0.5893 * 0.5893);
    return ior_d + cauchy_b * (1.0 / (lambda_um * lambda_um) - inv_d2);
}

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

/* holo_fresnel: the real equations, s and p separately. */
void fresnel(float cos_i, float n1, float n2, out float rs, out float rp) {
    float sin_i = sqrt(1.0 - cos_i * cos_i);
    float sin_t = (n1 / n2) * sin_i;
    if (sin_t >= 1.0) {
        rs = 1.0; rp = 1.0;
        return;
    }
    float cos_t = sqrt(1.0 - sin_t * sin_t);
    float rs_amp = (n1 * cos_i - n2 * cos_t) / (n1 * cos_i + n2 * cos_t);
    float rp_amp = (n1 * cos_t - n2 * cos_i) / (n1 * cos_t + n2 * cos_i);
    rs = rs_amp * rs_amp;
    rp = rp_amp * rp_amp;
}

/* nearest_hit: where, which way it faces, what it is made of, and whether
   its glass is a volume (spheres) or a thin pane (rects). */
bool nearest_hit(float3 ro, float3 rd,
                 out float best_t, out float3 best_n,
                 out float3 albedo, out float mirror,
                 out float transmit, out float ior, out float disperse,
                 out bool volume) {
    bool found = false;
    best_t = 1e30; best_n = float3(0, 0, 0);
    albedo = float3(0, 0, 0); mirror = 0;
    transmit = 0; ior = 1; disperse = 0; volume = false;

    float t; float3 n;
    for (int i = 0; i < (int)sphere_count; i++) {
        if (ray_sphere(ro, rd, sph_center_radius[i].xyz,
                       sph_center_radius[i].w, t, n) && t < best_t) {
            best_t = t; best_n = n;
            albedo = sph_albedo_mirror[i].xyz;
            mirror = sph_albedo_mirror[i].w;
            transmit = sph_glass[i].x;
            ior = sph_glass[i].y;
            disperse = sph_glass[i].z;
            volume = true;
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
            transmit = rect_glass[j].x;
            ior = rect_glass[j].y;
            disperse = rect_glass[j].z;
            volume = false;
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
        transmit = 0; ior = 1; disperse = 0; volume = false;
        found = true;
    }
    return found;
}

/* sun_blocked: opaque spheres or panels between the point and the sun.
   Mostly-clear glass throws no hard shadow (caustics are M8's problem). */
bool sun_blocked(float3 p) {
    float t; float3 n;
    for (int i = 0; i < (int)sphere_count; i++) {
        if (sph_glass[i].x <= 0.5 &&
            ray_sphere(p, sun_dir, sph_center_radius[i].xyz,
                       sph_center_radius[i].w, t, n)) {
            return true;
        }
    }
    for (int j = 0; j < (int)rect_count; j++) {
        if (rect_glass[j].x <= 0.5 &&
            ray_rect(p, sun_dir, rect_corner_mirror[j].xyz,
                     rect_edge_u[j].xyz, rect_edge_v[j].xyz, t, n)) {
            return true;
        }
    }
    return false;
}

float3 sky(float3 dir) {
    return lerp(horizon, zenith, 0.5 * (dir.y + 1.0));
}

/* holo_trace_ray: the stack walk, matching cpu_trace.c's caps, push order
   (refraction below reflection) and cull threshold exactly -- the two sides
   must drop the same branches. */
float3 trace(float3 ro, float3 rd) {
    float3 st_ro[16]; float3 st_rd[16]; float3 st_tp[16];
    int st_inside[16]; int st_depth[16];
    int sp = 0, processed = 0;
    float3 color = float3(0, 0, 0);

    st_ro[0] = ro; st_rd[0] = rd; st_tp[0] = float3(1, 1, 1);
    st_inside[0] = 0; st_depth[0] = 0;
    sp = 1;

    [loop] while (sp > 0 && processed < MAX_RAYS) {
        processed++;
        sp--;
        float3 p_ro = st_ro[sp]; float3 p_rd = st_rd[sp];
        float3 p_tp = st_tp[sp];
        int p_inside = st_inside[sp]; int p_depth = st_depth[sp];

        float best_t; float3 best_n; float3 albedo;
        float mirror; float transmit; float ior; float disperse; bool volume;
        if (!nearest_hit(p_ro, p_rd, best_t, best_n, albedo, mirror,
                         transmit, ior, disperse, volume)) {
            color += p_tp * sky(p_rd);
            continue;
        }
        float3 p = p_ro + best_t * p_rd;

        float matte = 1.0 - mirror - transmit;
        if (matte > 0.0) {
            float diffuse = max(dot(best_n, sun_dir), 0.0);
            if (diffuse > 0.0 && sun_blocked(p)) diffuse = 0.0;
            float3 lambert = albedo * (AMBIENT + (1.0 - AMBIENT) * diffuse);
            color += p_tp * lambert * matte;
        }

        if (p_depth >= MAX_BOUNCE) continue;

        float3 reflect_tint = albedo * mirror;
        if (transmit > 0.0) {
            float cos_i = -dot(best_n, p_rd);
            float n1 = p_inside ? ior : 1.0;
            float n2 = p_inside ? 1.0 : ior;
            float rs, rp;
            fresnel(cos_i, n1, n2, rs, rp);
            float r = 0.5 * (rs + rp);

            if (r < 1.0) {
                float3 tp = p_tp * albedo * (transmit * (1.0 - r));
                if (max(tp.x, max(tp.y, tp.z)) > MIN_TP && sp < STACK) {
                    if (volume) {
                        float eta = n1 / n2;
                        float k = 1.0 - eta * eta * (1.0 - cos_i * cos_i);
                        st_rd[sp] = eta * p_rd - (eta * -cos_i + sqrt(k)) * best_n;
                        st_inside[sp] = p_inside ? 0 : 1;
                    } else {
                        st_rd[sp] = p_rd;
                        st_inside[sp] = p_inside;
                    }
                    st_ro[sp] = p;
                    st_tp[sp] = tp;
                    st_depth[sp] = p_depth + 1;
                    sp++;
                }
            }
            reflect_tint += float3(1, 1, 1) * (transmit * r);
        }

        float3 rtp = p_tp * reflect_tint;
        if (max(rtp.x, max(rtp.y, rtp.z)) > MIN_TP && sp < STACK) {
            st_ro[sp] = p;
            st_rd[sp] = reflect(p_rd, best_n);
            st_tp[sp] = rtp;
            st_inside[sp] = p_inside;
            st_depth[sp] = p_depth + 1;
            sp++;
        }
    }
    return color;
}

/* holo_trace_lambda: the same walk with a scalar throughput -- one
   wavelength's worth of light, refracting at n(lambda). */
float trace_lambda(float3 ro, float3 rd, float lambda_um) {
    float3 st_ro[16]; float3 st_rd[16]; float st_tp[16];
    int st_inside[16]; int st_depth[16];
    int sp = 0, processed = 0;
    float intensity = 0.0;

    st_ro[0] = ro; st_rd[0] = rd; st_tp[0] = 1.0;
    st_inside[0] = 0; st_depth[0] = 0;
    sp = 1;

    [loop] while (sp > 0 && processed < MAX_RAYS) {
        processed++;
        sp--;
        float3 p_ro = st_ro[sp]; float3 p_rd = st_rd[sp];
        float p_tp = st_tp[sp];
        int p_inside = st_inside[sp]; int p_depth = st_depth[sp];

        float best_t; float3 best_n; float3 albedo;
        float mirror; float transmit; float ior; float disperse; bool volume;
        if (!nearest_hit(p_ro, p_rd, best_t, best_n, albedo, mirror,
                         transmit, ior, disperse, volume)) {
            intensity += p_tp * albedo_at(sky(p_rd), lambda_um);
            continue;
        }
        float3 p = p_ro + best_t * p_rd;

        float matte = 1.0 - mirror - transmit;
        if (matte > 0.0) {
            float diffuse = max(dot(best_n, sun_dir), 0.0);
            if (diffuse > 0.0 && sun_blocked(p)) diffuse = 0.0;
            float lambert = albedo_at(albedo, lambda_um)
                          * (AMBIENT + (1.0 - AMBIENT) * diffuse);
            intensity += p_tp * lambert * matte;
        }

        if (p_depth >= MAX_BOUNCE) continue;

        float reflect_tint = albedo_at(albedo, lambda_um) * mirror;
        if (transmit > 0.0) {
            float n_glass = ior_at(ior, disperse, lambda_um);
            float cos_i = -dot(best_n, p_rd);
            float n1 = p_inside ? n_glass : 1.0;
            float n2 = p_inside ? 1.0 : n_glass;
            float rs, rp;
            fresnel(cos_i, n1, n2, rs, rp);
            float r = 0.5 * (rs + rp);

            if (r < 1.0) {
                float ttp = p_tp * albedo_at(albedo, lambda_um)
                          * transmit * (1.0 - r);
                if (ttp > MIN_TP && sp < STACK) {
                    if (volume) {
                        float eta = n1 / n2;
                        float k = 1.0 - eta * eta * (1.0 - cos_i * cos_i);
                        st_rd[sp] = eta * p_rd - (eta * -cos_i + sqrt(k)) * best_n;
                        st_inside[sp] = p_inside ? 0 : 1;
                    } else {
                        st_rd[sp] = p_rd;
                        st_inside[sp] = p_inside;
                    }
                    st_ro[sp] = p;
                    st_tp[sp] = ttp;
                    st_depth[sp] = p_depth + 1;
                    sp++;
                }
            }
            reflect_tint += transmit * r;
        }

        float rtp = p_tp * reflect_tint;
        if (rtp > MIN_TP && sp < STACK) {
            st_ro[sp] = p;
            st_rd[sp] = reflect(p_rd, best_n);
            st_tp[sp] = rtp;
            st_inside[sp] = p_inside;
            st_depth[sp] = p_depth + 1;
            sp++;
        }
    }
    return intensity;
}

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    /* holo_camera_ray, with the aspect derived from res on both sides. */
    float x = (uv.x * 2.0 - 1.0) * tan_half_fov * (res.x / res.y);
    float y = (1.0 - uv.y * 2.0) * tan_half_fov;
    float3 rd = normalize(cam_fwd + cam_right * x + cam_up * y);

    if (spectral > 0.5) {
        float3 color = float3(0, 0, 0);
        [loop] for (int i = 0; i < WAVELENGTHS; i++) {
            float in_i = trace_lambda(cam_pos, rd, spectral_lw[i].x);
            color += spectral_lw[i].yzw * in_i;
        }
        return float4(color, 1.0);
    }
    return float4(trace(cam_pos, rd), 1.0);
}
