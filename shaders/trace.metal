/* The tracer in Metal Shading Language, the third twin of
 * source/cpu_trace.c, shaders/trace.hlsl and shaders/trace.glsl. All four
 * must stay statement for statement the same: the oracle diff holds a GPU
 * frame to the CPU's pixels, and a change that lands in one file and not the
 * others is what that diff exists to catch.
 *
 * ---------------------------------------------------------------------------
 * The two places this file cannot look like its twins
 *
 * 1. MSL has no global uniforms. A buffer is an argument to the entry point,
 *    so `params` is threaded explicitly into every function that reads the
 *    scene -- ray_floor, nearest_hit, sun_blocked, sky, trace, trace_lambda --
 *    as a first `constant float4 *params` argument. The function BODIES are
 *    unchanged from the GLSL, because the accessor macros below only need a
 *    thing called `params` to be in scope, and now that thing is a parameter
 *    rather than a global. Call sites gain a `params,` and nothing else.
 *
 * 2. MSL writes an out parameter as `thread T &x` rather than `out T x`.
 *
 * Everything else -- the slot map, the walk, the caps, the push order, the
 * cull thresholds -- is the GLSL verbatim.
 *
 * ---------------------------------------------------------------------------
 * Bindings, which must agree with source/display.c
 *
 * The uniform block is at [[buffer(0)]] because display.c sets the block's
 * msl_buffer_n to 0, and sokol binds a fragment-stage block with
 * setFragmentBuffer:atIndex: at exactly that index.
 *
 * The varying carries [[user(locn0)]] because the two stages are compiled as
 * SEPARATE Metal libraries (display.c hands sokol one source string per
 * stage), and across separate libraries Metal matches a vertex output to a
 * fragment input by attribute, not by name. display.c's vertex source tags
 * its `uv` the same way. Both stages are named main0: MSL has no main(), and
 * each library gets its own namespace, so the name can be reused.
 *
 * ---------------------------------------------------------------------------
 * Status: NOT yet held to the oracle. This file was written from the GLSL
 * tracer, which is oracle-green on all eight example scenes, but no Metal
 * device has compiled or run it. Before trusting it, on a Mac:
 *
 *     ./build.sh && ./build/m7_room --diff
 *
 * and expect the numbers in the m7_room row of the README's oracle table.
 */

#include <metal_stdlib>
using namespace metal;

/* HoloDisplayUniforms, the header every hologram shader receives. */
#define res                 params[0].xy
#define time                params[0].z

/* The camera and the world, one float3 + one float per slot. */
#define cam_pos             params[1].xyz
#define tan_half_fov        params[1].w
#define cam_fwd             params[2].xyz
#define spectral            params[2].w
#define cam_right           params[3].xyz
#define sphere_count        params[3].w
#define cam_up              params[4].xyz
#define has_floor           params[4].w
#define sun_dir             params[5].xyz
#define floor_y             params[5].w
#define horizon             params[6].xyz
#define rect_count          params[6].w
#define zenith              params[7].xyz
#define floor_mirror        params[7].w
#define floor_a             params[8].xyz
#define sun_disk_cos        params[8].w
#define floor_b             params[9].xyz
#define sun_disk_intensity  params[9].w

/* The primitive arrays. Written as calls -- sph_glass(i) -- because a
   function-like macro is the only way to keep the field name in front of a
   slot offset. tests/test_gpu_layout.c holds these slots to the C struct. */
#define sph_center_radius(i)   params[10 + (i)]
#define sph_albedo_mirror(i)   params[18 + (i)]
#define sph_glass(i)           params[26 + (i)]
#define rect_corner_mirror(i)  params[34 + (i)]
#define rect_edge_u(i)         params[42 + (i)]
#define rect_edge_v(i)         params[50 + (i)]
#define rect_albedo(i)         params[58 + (i)]
#define rect_glass(i)          params[66 + (i)]
#define rect_filter(i)         params[74 + (i)]
#define dish_apex_r(i)         params[82 + (i)]
#define dish_axis_k(i)         params[86 + (i)]
#define dish_albedo_mirror(i)  params[90 + (i)]
#define dish_rim_count(i)      params[94 + (i)]
#define spectral_lw(i)         params[98 + (i)]

/* The gratings sit in scalar slots, not a dynamically indexed array: see the
   note in trace.hlsl. Metal has no such defect, but the tracers stay
   identical in structure so the oracle diff keeps its meaning. */
#define grat0_groove_idx    params[110]
#define grat0_period_w      params[111]
#define grat1_groove_idx    params[112]
#define grat1_period_w      params[113]
#define grat_w2             params[114]

/* Globals must live in an address space; these are compile-time constants. */
constant float T_MIN = 1e-3;      /* HOLO_T_MIN */
constant float AMBIENT = 0.1;     /* HOLO_AMBIENT */
constant int   MAX_BOUNCE = 16;   /* HOLO_MAX_BOUNCE */
constant int   MAX_RAYS = 32;     /* HOLO_MAX_RAYS */
constant int   STACK = 16;        /* HOLO_STACK */
constant float MIN_TP = 0.002;    /* HOLO_MIN_TP */
constant int   WAVELENGTHS = 12;  /* HOLO_WAVELENGTHS */

/* Matched to display.c's vertex stage by attribute, not by name. */
struct fs_in {
    float2 uv [[user(locn0)]];
};

/* holo_albedo_at: an RGB color read at one wavelength through three smooth
   bands that partition unity -- neutral colors are exact. */
float albedo_at(float3 rgb, float lambda_um) {
    float t_bg = clamp((lambda_um - 0.475) / (0.510 - 0.475), 0.0, 1.0);
    float t_gr = clamp((lambda_um - 0.565) / (0.610 - 0.565), 0.0, 1.0);
    return rgb.r * t_gr + rgb.g * (t_bg - t_gr) + rgb.b * (1.0 - t_bg);
}

/* holo_ior_at: Cauchy dispersion around the sodium D line. */
float ior_at(float ior_d, float cauchy_b, float lambda_um) {
    const float inv_d2 = 1.0 / (0.5893 * 0.5893);
    return ior_d + cauchy_b * (1.0 / (lambda_um * lambda_um) - inv_d2);
}

/* holo_ray_sphere. Writes t and the normal on the arriving side. */
bool ray_sphere(float3 ro, float3 rd, float3 center, float radius,
                thread float &t, thread float3 &normal) {
    t = 0.0; normal = float3(0.0, 0.0, 0.0);
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
              thread float &t, thread float3 &normal) {
    t = 0.0;
    normal = normalize(cross(eu, ev));
    float denom = dot(normal, rd);
    if (abs(denom) < 1e-8) return false;
    t = dot(corner - ro, normal) / denom;
    if (t <= T_MIN) return false;
    float3 rel = ro + t * rd - corner;
    float uu = dot(eu, eu), vv = dot(ev, ev), uv_dot = dot(eu, ev);
    float ru = dot(rel, eu), rv = dot(rel, ev);
    float det = uu * vv - uv_dot * uv_dot;
    float u = (ru * vv - rv * uv_dot) / det;
    float v = (rv * uu - ru * uv_dot) / det;
    if (u < 0.0 || u > 1.0 || v < 0.0 || v > 1.0) return false;
    if (denom > 0.0) normal = -normal;
    return true;
}

/* holo_ray_dish: a cap of a conic of revolution, quadratic along the ray
   in the dish's own frame. Ported statement for statement. */
bool ray_dish(float3 ro, float3 rd, float3 apex, float3 axis,
              float curv_r, float conic_k, float rim,
              thread float &t_out, thread float3 &normal) {
    t_out = 0.0; normal = float3(0.0, 0.0, 0.0);
    float3 helper = abs(axis.x) > 0.9 ? float3(0.0, 1.0, 0.0)
                                      : float3(1.0, 0.0, 0.0);
    float3 u = normalize(cross(helper, axis));
    float3 v = cross(axis, u);
    float3 rel = ro - apex;
    float3 o = float3(dot(rel, u), dot(rel, v), dot(rel, axis));
    float3 d = float3(dot(rd, u), dot(rd, v), dot(rd, axis));

    float p = 1.0 + conic_k;
    float A = d.x * d.x + d.y * d.y + p * d.z * d.z;
    float B = o.x * d.x + o.y * d.y + p * o.z * d.z - curv_r * d.z;
    float C = o.x * o.x + o.y * o.y + p * o.z * o.z - 2.0 * curv_r * o.z;

    float rr = rim * rim;
    float root = 1.0 - p * rr / (curv_r * curv_r);
    float z_max = rr / (curv_r * (1.0 + sqrt(max(root, 0.0))));

    float t1, t2;
    if (abs(A) < 1e-8) {
        if (abs(B) < 1e-12) return false;
        t1 = -C / (2.0 * B);
        t2 = -1.0;
    } else {
        float disc = B * B - A * C;
        if (disc < 0.0) return false;
        float sq = sqrt(disc);
        t1 = (-B - sq) / A;
        t2 = (-B + sq) / A;
    }

    for (int side = 0; side < 2; side++) {
        float t = side == 0 ? t1 : t2;
        if (t <= T_MIN) continue;
        float z = o.z + t * d.z;
        float x = o.x + t * d.x;
        float y = o.y + t * d.y;
        if (z < 0.0 || z > z_max || x * x + y * y > rr) continue;
        t_out = t;
        float3 n = normalize(u * x + v * y + axis * (p * z - curv_r));
        normal = dot(n, rd) < 0.0 ? n : -n;
        return true;
    }
    return false;
}

/* holo_ray_plane, for the y-up floor only (all any scene has). */
bool ray_floor(constant float4 *params, float3 ro, float3 rd,
               thread float &t) {
    t = 0.0;
    if (abs(rd.y) < 1e-8) return false;
    t = (floor_y - ro.y) / rd.y;
    return t > T_MIN;
}

/* holo_fresnel: the real equations, s and p separately. */
void fresnel(float cos_i, float n1, float n2,
             thread float &rs, thread float &rp) {
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
bool nearest_hit(constant float4 *params, float3 ro, float3 rd,
                 thread float &best_t, thread float3 &best_n,
                 thread float3 &albedo, thread float &mirror,
                 thread float &transmit, thread float &ior,
                 thread float &disperse,
                 thread bool &volume, thread int &rect_id) {
    bool found = false;
    best_t = 1e30; best_n = float3(0.0, 0.0, 0.0);
    albedo = float3(0.0, 0.0, 0.0); mirror = 0.0;
    transmit = 0.0; ior = 1.0; disperse = 0.0; volume = false;
    rect_id = -1;

    float t; float3 n;
    for (int i = 0; i < int(sphere_count); i++) {
        if (ray_sphere(ro, rd, sph_center_radius(i).xyz,
                       sph_center_radius(i).w, t, n) && t < best_t) {
            best_t = t; best_n = n;
            albedo = sph_albedo_mirror(i).xyz;
            mirror = sph_albedo_mirror(i).w;
            transmit = sph_glass(i).x;
            ior = sph_glass(i).y;
            disperse = sph_glass(i).z;
            volume = true;
            found = true;
            rect_id = -1;
        }
    }
    for (int j = 0; j < int(rect_count); j++) {
        if (ray_rect(ro, rd, rect_corner_mirror(j).xyz,
                     rect_edge_u(j).xyz, rect_edge_v(j).xyz, t, n) &&
            t < best_t) {
            best_t = t; best_n = n;
            albedo = rect_albedo(j).xyz;
            mirror = rect_corner_mirror(j).w;
            transmit = rect_glass(j).x;
            ior = rect_glass(j).y;
            disperse = rect_glass(j).z;
            volume = false;
            found = true;
            rect_id = j;
        }
    }
    for (int k = 0; k < int(dish_rim_count(0).y); k++) {
        if (ray_dish(ro, rd, dish_apex_r(k).xyz, dish_axis_k(k).xyz,
                     dish_apex_r(k).w, dish_axis_k(k).w,
                     dish_rim_count(k).x, t, n) && t < best_t) {
            best_t = t; best_n = n;
            albedo = dish_albedo_mirror(k).xyz;
            mirror = dish_albedo_mirror(k).w;
            transmit = 0.0; ior = 1.0; disperse = 0.0; volume = false;
            found = true;
            rect_id = -1;
        }
    }
    if (has_floor > 0.5 && ray_floor(params, ro, rd, t) && t < best_t) {
        best_t = t;
        best_n = float3(0.0, rd.y < 0.0 ? 1.0 : -1.0, 0.0);
        float3 p = ro + t * rd;
        int cell = int(floor(p.x)) + int(floor(p.z));
        albedo = (cell & 1) != 0 ? floor_b : floor_a;
        mirror = floor_mirror;
        transmit = 0.0; ior = 1.0; disperse = 0.0; volume = false;
        found = true;
        rect_id = -1;
    }
    return found;
}

/* sun_blocked: opaque spheres or panels between the point and the sun.
   Mostly-clear glass throws no hard shadow (caustics are M8's problem). */
bool sun_blocked(constant float4 *params, float3 p) {
    float t; float3 n;
    for (int i = 0; i < int(sphere_count); i++) {
        if (sph_glass(i).x <= 0.5 &&
            ray_sphere(p, sun_dir, sph_center_radius(i).xyz,
                       sph_center_radius(i).w, t, n)) {
            return true;
        }
    }
    for (int j = 0; j < int(rect_count); j++) {
        if (rect_glass(j).x <= 0.5 && rect_filter(j).x < 0.5 &&
            ray_rect(p, sun_dir, rect_corner_mirror(j).xyz,
                     rect_edge_u(j).xyz, rect_edge_v(j).xyz, t, n)) {
            return true;
        }
    }
    return false;
}

float3 sky(constant float4 *params, float3 dir) {
    if (sun_disk_intensity > 0.0 && dot(dir, sun_dir) > sun_disk_cos) {
        return float3(sun_disk_intensity, sun_disk_intensity,
                      sun_disk_intensity);
    }
    return mix(horizon, zenith, 0.5 * (dir.y + 1.0));
}

/* holo_trace_ray: the stack walk, matching cpu_trace.c's caps, push order
   (refraction below reflection) and cull threshold exactly -- the two sides
   must drop the same branches. */
float3 trace(constant float4 *params, float3 ro, float3 rd) {
    float3 st_ro[16]; float3 st_rd[16]; float3 st_tp[16];
    int st_inside[16]; int st_depth[16];
    int sp = 0, processed = 0;
    float3 color = float3(0.0, 0.0, 0.0);

    st_ro[0] = ro; st_rd[0] = rd; st_tp[0] = float3(1.0, 1.0, 1.0);
    st_inside[0] = 0; st_depth[0] = 0;
    sp = 1;

    while (sp > 0 && processed < MAX_RAYS) {
        processed++;
        sp--;
        float3 p_ro = st_ro[sp]; float3 p_rd = st_rd[sp];
        float3 p_tp = st_tp[sp];
        int p_inside = st_inside[sp]; int p_depth = st_depth[sp];

        float best_t; float3 best_n; float3 albedo;
        float mirror; float transmit; float ior; float disperse; bool volume;
        int f_rect;
        if (!nearest_hit(params, p_ro, p_rd, best_t, best_n, albedo, mirror,
                         transmit, ior, disperse, volume, f_rect)) {
            color += p_tp * sky(params, p_rd);
            continue;
        }
        float3 p = p_ro + best_t * p_rd;

        /* The RGB path has no wavelength: a grating shows only its
           specular order at that order's weight, read from its scalar
           slot. */
        if (f_rect >= 0 && (f_rect == int(grat0_groove_idx.w) ||
                            f_rect == int(grat1_groove_idx.w))) {
            float w0 = f_rect == int(grat0_groove_idx.w)
                     ? grat0_period_w.z : grat1_period_w.z;
            float3 gtp = p_tp * w0;
            if (max(gtp.x, max(gtp.y, gtp.z)) > MIN_TP && sp < STACK &&
                p_depth < MAX_BOUNCE) {
                st_ro[sp] = p; st_rd[sp] = reflect(p_rd, best_n);
                st_tp[sp] = gtp;
                st_inside[sp] = p_inside; st_depth[sp] = p_depth + 1;
                sp++;
            }
            continue;
        }

        /* The RGB path approximates filters: a polarizer is a flat 50%,
           a waveplate is clear. The spectral walk does them right. */
        int f_mode = f_rect >= 0 ? int(rect_filter(f_rect).x) : 0;
        if (f_mode == 1 || f_mode == 2) {
            float3 ftp = p_tp * (f_mode == 1 ? 0.5 : 1.0);
            if (max(ftp.x, max(ftp.y, ftp.z)) > MIN_TP && sp < STACK &&
                p_depth < MAX_BOUNCE) {
                st_ro[sp] = p; st_rd[sp] = p_rd;
                st_tp[sp] = ftp;
                st_inside[sp] = p_inside; st_depth[sp] = p_depth + 1;
                sp++;
            }
            continue;
        }

        float matte = 1.0 - mirror - transmit;
        if (matte > 0.0) {
            float diffuse = max(dot(best_n, sun_dir), 0.0);
            if (diffuse > 0.0 && sun_blocked(params, p)) diffuse = 0.0;
            float3 lambert = albedo * (AMBIENT + (1.0 - AMBIENT) * diffuse);
            color += p_tp * lambert * matte;
        }

        if (p_depth >= MAX_BOUNCE) continue;

        float3 reflect_tint = albedo * mirror;
        if (transmit > 0.0) {
            float cos_i = -dot(best_n, p_rd);
            float n1 = p_inside != 0 ? ior : 1.0;
            float n2 = p_inside != 0 ? 1.0 : ior;
            float rs, rp;
            fresnel(cos_i, n1, n2, rs, rp);
            float r = 0.5 * (rs + rp);

            if (r < 1.0) {
                float3 tp = p_tp * albedo * (transmit * (1.0 - r));
                if (max(tp.x, max(tp.y, tp.z)) > MIN_TP && sp < STACK) {
                    bool refracted = true;
                    if (volume) {
                        /* Checked like the CPU: at the razor edge of the
                           critical angle Fresnel can say "not TIR" while
                           this discriminant disagrees. */
                        float eta = n1 / n2;
                        float k = 1.0 - eta * eta * (1.0 - cos_i * cos_i);
                        refracted = k >= 0.0;
                        st_rd[sp] = eta * p_rd
                                  - (eta * -cos_i + sqrt(max(k, 0.0)))
                                    * best_n;
                        st_inside[sp] = p_inside != 0 ? 0 : 1;
                    } else {
                        st_rd[sp] = p_rd;
                        st_inside[sp] = p_inside;
                    }
                    if (refracted) {
                        st_ro[sp] = p;
                        st_tp[sp] = tp;
                        st_depth[sp] = p_depth + 1;
                        sp++;
                    }
                }
            }
            reflect_tint += float3(1.0, 1.0, 1.0) * (transmit * r);
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

/* holo_grating_order: the conical/off-plane vector grating equation. The
   groove component is conserved, the dispersion component picks up
   m lambda/d, the normal component rebalances; m = 0 is exact specular.
   Returns false when the order is evanescent. */
bool grating_order(float3 d, float3 n, float3 groove, float m_lambda_over_d,
                   thread float3 &outdir) {
    outdir = float3(0.0, 0.0, 0.0);
    float3 q = cross(groove, n);
    float alpha = dot(d, q) + m_lambda_over_d;
    float beta = dot(d, groove);
    float rem = 1.0 - alpha * alpha - beta * beta;
    /* NaN-safe: written as !(rem > 0) so a NaN from a speculated 0/0 --
       fxc runs both sides of divergent branches -- reads as evanescent
       instead of as a propagating NaN direction. */
    if (!(rem > 0.0)) return false;
    outdir = q * alpha + groove * beta + n * sqrt(rem);
    return true;
}

/* polar.c ported: the detector-row Stokes ops. srow' = srow * M. */
float4 srow_rotate(float4 s, float c2, float s2) {
    return float4(s.x, c2 * s.y - s2 * s.z, s2 * s.y + c2 * s.z, s.w);
}

float4 srow_mueller(float4 s, float a, float b, float c, float d) {
    return float4(a * s.x + b * s.y, b * s.x + a * s.y,
                  c * s.z - d * s.w, d * s.z + c * s.w);
}

float4 srow_polarizer(float4 s) {
    float half_iq = 0.5 * (s.x + s.y);
    return float4(half_iq, half_iq, 0.0, 0.0);
}

void frame_rot(float3 frame, float3 target, float3 dir,
               thread float &c2, thread float &s2) {
    float c = dot(frame, target);
    float s = dot(cross(frame, target), dir);
    c2 = c * c - s * s;
    s2 = 2.0 * c * s;
}

/* holo_fresnel_amp: amplitudes, the power-projection factor, and the TIR
   phase. Returns true past the critical angle. */
bool fresnel_amp(float cos_i, float n1, float n2,
                 thread float &rs, thread float &rp, thread float &ts,
                 thread float &tp, thread float &f, thread float &delta) {
    float sin_i2 = 1.0 - cos_i * cos_i;
    float sin_t = (n1 / n2) * sqrt(sin_i2);
    if (sin_t >= 1.0) {
        float n = n2 / n1;
        float g = sqrt(sin_i2 - n * n);
        rs = 1.0; rp = 1.0; ts = 0.0; tp = 0.0; f = 0.0;
        delta = 2.0 * (atan(g / (n * n * cos_i)) - atan(g / cos_i));
        return true;
    }
    float cos_t = sqrt(1.0 - sin_t * sin_t);
    rs = (n1 * cos_i - n2 * cos_t) / (n1 * cos_i + n2 * cos_t);
    rp = (n1 * cos_t - n2 * cos_i) / (n1 * cos_t + n2 * cos_i);
    ts = 2.0 * n1 * cos_i / (n1 * cos_i + n2 * cos_t);
    tp = 2.0 * n1 * cos_i / (n1 * cos_t + n2 * cos_i);
    f = (n2 * cos_t) / (n1 * cos_i);
    delta = 0.0;
    return false;
}

float3 initial_frame(float3 dir) {
    float3 f = cross(dir, float3(0.0, 1.0, 0.0));
    if (dot(f, f) < 1e-6) f = cross(dir, float3(1.0, 0.0, 0.0));
    return normalize(f);
}

/* holo_trace_lambda: CHAIN + FORK. Each popped ray walks as a chain --
   every surface continues IN PLACE (reflection off glass and mirrors,
   transmission through filters, the current order off a grating) and
   forks AT MOST ONE side branch onto the stack (glass's transmitted ray,
   a grating's next-order revisit). One push per interaction is a hard
   ceiling: fxc corrupts the stack arrays when any single branch pushes
   twice. Caps, culls and push order match cpu_trace.c exactly. */
float trace_lambda(constant float4 *params, float3 ro, float3 rd,
                   float lambda_um) {
    float3 st_ro[16]; float3 st_rd[16];
    float st_si[16]; float st_sq[16]; float st_su[16]; float st_sv[16];
    float3 st_frame[16];
    int st_inside[16]; int st_depth[16];
    int sp = 0, processed = 0;
    float intensity = 0.0;

    st_ro[0] = ro; st_rd[0] = rd;
    st_si[0] = 1.0; st_sq[0] = 0.0; st_su[0] = 0.0; st_sv[0] = 0.0;
    st_frame[0] = initial_frame(rd);
    st_inside[0] = 0; st_depth[0] = 0;
    sp = 1;

    while (sp > 0 && processed < MAX_RAYS) {
        sp--;
        float3 p_ro = st_ro[sp]; float3 p_rd = st_rd[sp];
        float4 p_srow = float4(st_si[sp], st_sq[sp], st_su[sp], st_sv[sp]);
        float3 p_frame = st_frame[sp];
        int p_inside = st_inside[sp];
        /* Depth in the low bits, the grating order index in the high. */
        int p_depth = st_depth[sp] & 0xFF;
        int p_order = st_depth[sp] >> 8;

        while (processed < MAX_RAYS) {
            processed++;

            float best_t; float3 best_n; float3 albedo;
            float mirror; float transmit; float ior; float disperse;
            bool volume;
            int rect_i;
            if (!nearest_hit(params, p_ro, p_rd, best_t, best_n, albedo,
                             mirror, transmit, ior, disperse, volume,
                             rect_i)) {
                intensity += p_srow.x * albedo_at(sky(params, p_rd),
                                                  lambda_um);
                break;
            }
            float3 p = p_ro + best_t * p_rd;

            /* A grating: the chain follows THIS visit's order, the fork
               revisits the surface for the next. Constants come from the
               grating's SCALAR slot, matched by rect index. */
            if (rect_i >= 0 && (rect_i == int(grat0_groove_idx.w) ||
                                rect_i == int(grat1_groove_idx.w))) {
                if (p_depth >= MAX_BOUNCE) break;
                bool slot0 = rect_i == int(grat0_groove_idx.w);
                float3 groove = slot0 ? grat0_groove_idx.xyz
                                      : grat1_groove_idx.xyz;
                float period = max(slot0 ? grat0_period_w.x
                                         : grat1_period_w.x, 1e-6);
                float4 wts = slot0
                    ? float4(grat0_period_w.yzw, grat_w2.x)
                    : float4(grat1_period_w.yzw, grat_w2.y);
                if (p_order < 3 && sp < STACK) {
                    st_ro[sp] = p_ro; st_rd[sp] = p_rd;
                    st_si[sp] = p_srow.x; st_sq[sp] = p_srow.y;
                    st_su[sp] = p_srow.z; st_sv[sp] = p_srow.w;
                    st_frame[sp] = p_frame;
                    st_inside[sp] = p_inside;
                    st_depth[sp] = p_depth | ((p_order + 1) << 8);
                    sp++;
                }
                float m_f = p_order == 0 ? -1.0 : p_order == 1 ? 0.0
                          : p_order == 2 ? 1.0 : 2.0;
                float w = p_order == 0 ? wts.x : p_order == 1 ? wts.y
                        : p_order == 2 ? wts.z : wts.w;
                float3 outdir;
                if (!grating_order(p_rd, best_n, groove,
                                   m_f * lambda_um / period, outdir)) {
                    break;   /* this order is evanescent */
                }
                float4 gs = p_srow * w;
                if (gs.x <= MIN_TP) break;
                p_ro = p; p_rd = outdir;
                p_srow = gs;
                p_frame = initial_frame(outdir);
                p_depth++;
                p_order = 0;
                continue;
            }

            /* Filters: rotate into the pane's axis, apply its Mueller,
               carry the chain on straight. */
            int filter_mode = rect_i >= 0 ? int(rect_filter(rect_i).x) : 0;
            if (filter_mode == 1 || filter_mode == 2) {
                if (p_depth >= MAX_BOUNCE) break;
                float3 in_pane = rect_filter(rect_i).yzw;
                float3 axis_t = in_pane - p_rd * dot(in_pane, p_rd);
                float3 axis = dot(axis_t, axis_t) < 1e-6 ? p_frame
                                                         : normalize(axis_t);
                float c2, s2;
                frame_rot(p_frame, axis, p_rd, c2, s2);
                float4 fs = srow_rotate(p_srow, c2, s2);
                if (filter_mode == 1) {
                    fs = srow_polarizer(fs);
                } else {
                    float d = rect_glass(rect_i).w * 0.5893 / lambda_um;
                    fs = srow_mueller(fs, 1.0, 0.0, cos(d), sin(d));
                }
                if (fs.x <= MIN_TP) break;
                p_ro = p;
                p_srow = fs;
                p_frame = axis;
                p_depth++;
                p_order = 0;
                continue;
            }

            float matte = 1.0 - mirror - transmit;
            if (matte > 0.0) {
                float diffuse = max(dot(best_n, sun_dir), 0.0);
                if (diffuse > 0.0 && sun_blocked(params, p)) diffuse = 0.0;
                float lambert = albedo_at(albedo, lambda_um)
                              * (AMBIENT + (1.0 - AMBIENT) * diffuse);
                intensity += p_srow.x * lambert * matte;
            }

            if (p_depth >= MAX_BOUNCE) break;

            float3 s_hat = cross(p_rd, best_n);
            float c2 = 1.0, s2 = 0.0;
            if (dot(s_hat, s_hat) > 1e-6) {
                s_hat = normalize(s_hat);
                frame_rot(p_frame, s_hat, p_rd, c2, s2);
            } else {
                s_hat = p_frame;
            }
            float4 srow_sp = srow_rotate(p_srow, c2, s2);

            float mtint = albedo_at(albedo, lambda_um) * mirror;
            float ra = mtint, rb = 0.0, rc = mtint, rdd = 0.0;

            if (transmit > 0.0) {
                float n_glass = ior_at(ior, disperse, lambda_um);
                float cos_i = -dot(best_n, p_rd);
                float n1 = p_inside != 0 ? n_glass : 1.0;
                float n2 = p_inside != 0 ? 1.0 : n_glass;
                float rs, rp, ts, tpa, f, delta;
                bool tir = fresnel_amp(cos_i, n1, n2, rs, rp, ts, tpa,
                                       f, delta);

                if (!tir) {
                    /* The fork: the transmitted ray. */
                    float k = albedo_at(albedo, lambda_um) * transmit;
                    float4 st = srow_mueller(srow_sp,
                            k * 0.5 * f * (ts * ts + tpa * tpa),
                            k * 0.5 * f * (ts * ts - tpa * tpa),
                            k * f * ts * tpa, 0.0);
                    if (st.x > MIN_TP && sp < STACK) {
                        bool refracted = true;
                        if (volume) {
                            /* Checked like the CPU: at the razor edge of
                               the critical angle Fresnel can say "not TIR"
                               while this discriminant disagrees. */
                            float eta = n1 / n2;
                            float kk = 1.0 - eta * eta
                                     * (1.0 - cos_i * cos_i);
                            refracted = kk >= 0.0;
                            st_rd[sp] = eta * p_rd
                                      - (eta * -cos_i + sqrt(max(kk, 0.0)))
                                        * best_n;
                            st_inside[sp] = p_inside != 0 ? 0 : 1;
                        } else {
                            st_rd[sp] = p_rd;
                            st_inside[sp] = p_inside;
                        }
                        if (refracted) {
                            st_ro[sp] = p;
                            st_si[sp] = st.x; st_sq[sp] = st.y;
                            st_su[sp] = st.z; st_sv[sp] = st.w;
                            st_frame[sp] = s_hat;
                            st_depth[sp] = p_depth + 1;
                            sp++;
                        }
                    }
                    ra += transmit * 0.5 * (rs * rs + rp * rp);
                    rb += transmit * 0.5 * (rs * rs - rp * rp);
                    rc += transmit * rs * rp;
                } else {
                    ra += transmit;
                    rc += transmit * cos(delta);
                    rdd += transmit * sin(delta);
                }
            }

            /* The chain: the reflected ray. */
            float4 sr = srow_mueller(srow_sp, ra, rb, rc, rdd);
            if (sr.x <= MIN_TP) break;
            p_ro = p;
            p_rd = reflect(p_rd, best_n);
            p_srow = sr;
            p_frame = s_hat;
            p_depth++;
            p_order = 0;
        }
    }
    return intensity;
}

/* The tracer works in linear radiance; the swapchain is read as sRGB.
   Encoding at this one boundary is what keeps dim physics -- an eighth of
   a wall through three polarizers -- visible as the eye would see it.
   oracle.c applies the same curve before comparing. */
float3 srgb_encode(float3 c) {
    c = clamp(c, 0.0, 1.0);
    float3 lo = c * 12.92;
    float3 hi = 1.055 * pow(c, float3(1.0 / 2.4)) - 0.055;
    return float3(c.x <= 0.0031308 ? lo.x : hi.x,
                  c.y <= 0.0031308 ? lo.y : hi.y,
                  c.z <= 0.0031308 ? lo.z : hi.z);
}

fragment float4 main0(fs_in in [[stage_in]],
                      constant float4 *params [[buffer(0)]]) {
    /* holo_camera_ray, with the aspect derived from res on both sides. */
    float x = (in.uv.x * 2.0 - 1.0) * tan_half_fov * (res.x / res.y);
    float y = (1.0 - in.uv.y * 2.0) * tan_half_fov;
    float3 rd = normalize(cam_fwd + cam_right * x + cam_up * y);

    float3 color;
    if (spectral > 0.5) {
        color = float3(0.0, 0.0, 0.0);
        for (int i = 0; i < WAVELENGTHS; i++) {
            float in_i = trace_lambda(params, cam_pos, rd, spectral_lw(i).x);
            color += spectral_lw(i).yzw * in_i;
        }
    } else {
        color = trace(params, cam_pos, rd);
    }
    return float4(srgb_encode(color), 1.0);
}
