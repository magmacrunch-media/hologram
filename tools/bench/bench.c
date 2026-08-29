/* bench: what a panel costs, on the GPU, on the backend you actually ship.
 *
 * The oracle answers "is the tracer right". This answers "what does it cost",
 * which is the question a room full of mirrors raises: nearest_hit scans the
 * panels linearly, once per ray per bounce, and a full mirror keeps every ray
 * alive to the bounce cap. Panel count is a frame-time budget, not a capacity.
 *
 * It is also a shader A/B rig. Point it at two files that differ in one
 * respect and it will tell you which is faster, on real hardware, instead of
 * leaving the question to intuition -- which on this engine has been wrong
 * twice. Hoisting the rect basis was worth it; also shipping the precomputed
 * normal, which looks like the same kind of win, is 1.35x SLOWER at 64 panels
 * on both D3D11 and WebGL2, because the extra indexed constant fetch costs
 * more than the cross and normalize it saves.
 *
 *   build\bench.exe                        sweep 1..HOLO_MAX_RECTS
 *   build\bench.exe --shader other.hlsl    the same sweep, another tracer
 *   build\bench.exe --rects 8 --frames 600
 *
 * ---------------------------------------------------------------------------
 * On measuring this honestly
 *
 * Wall-clock frame time does not work. With vsync on, every number is the
 * refresh interval. With vsync off, sokol presents with DXGI_PRESENT_DO_NOT_
 * WAIT and the CPU runs ahead of the GPU, so the frame duration measures the
 * loop -- the first version of this tool cheerfully reported a 0.104ms frame
 * for work that takes 0.64ms.
 *
 * On D3D11 it therefore uses timestamp queries, which time the GPU and
 * nothing else. before_frame runs ahead of sg_begin_pass and after_frame
 * behind sg_commit, so an End() in each brackets exactly the pass. Elsewhere
 * it falls back to the frame clock, which includes CPU time and present, and
 * says so in its output rather than quietly reporting a worse number.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(SOKOL_D3D11)
    #include <d3d11.h>
#endif
#include "external/sokol/sokol_app.h"
#include "external/sokol/sokol_gfx.h"
#include "hologram.h"

#define WARMUP 30
#define MAX_SAMPLES 4096
#define MAX_STAGES 16

static HoloGpuScene gpu;
static HoloScene scene;
static char shader_src[262144];

static int stage_rects[MAX_STAGES];
static int stage_count;
static double stage_ms[MAX_STAGES];
static int stage;

static int want_frames = 300;
static const char *shader_path;

static double samples[MAX_SAMPLES];
static int nsamples;
static int frames;
static int timing_is_gpu;

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : (x > y ? 1 : 0);
}

/* The scene: panels on a lattice, alternating orientation so a ray crossing
   it meets many of them and the mirrors face each other. Full mirrors,
   because that is both what the game wants and the worst case for the walk. */
static void build_scene(int rects) {
    scene = (HoloScene){
        .sphere_count = 0,
        .has_floor = 1,
        .floor_y = 0.0f,
        .floor_a = { 0.8f, 0.8f, 0.82f },
        .floor_b = { 0.22f, 0.26f, 0.3f },
        .floor_mirror = 0.12f,
        .sun_dir = { 0.169f, 0.507f, 0.845f },
        .horizon = { 0.95f, 0.9f, 0.85f },
        .zenith  = { 0.3f, 0.45f, 0.75f },
    };
    for (int i = 0; i < rects; i++) {
        int gx = i % 8, gz = i / 8;
        HoloRect r = { 0 };
        r.corner = hv3(-7.0f + 2.0f * (float)gx, 0.0f, -7.0f + 2.0f * (float)gz);
        r.edge_u = ((gx + gz) & 1) ? hv3(1.8f, 0, 0) : hv3(0, 0, 1.8f);
        r.edge_v = hv3(0, 2.6f, 0);
        r.albedo = hv3(0.88f, 0.92f, 0.95f);
        r.mirror = 1.0f;
        scene.rects[i] = r;
    }
    scene.rect_count = rects;

    HoloV3 eye = hv3(0.4f, 1.55f, 8.0f);
    HoloCamera cam = holo_camera_make(eye, hv3_add(eye, hv3(0, 0, -1)),
                                      hv3(0, 1, 0), 70.0f, 1.0f);
    holo_gpu_scene_fill(&gpu, &scene, &cam, 1);   /* spectral, as a game runs */
}

/* ---- the clock ---------------------------------------------------------- */

#if defined(SOKOL_D3D11)
static ID3D11Query *q_disjoint, *q_start, *q_end;
static int queries_ready;

static void clock_begin(void) {
    if (!queries_ready) {
        ID3D11Device *dev = (ID3D11Device *)sg_d3d11_device();
        if (!dev) {
            return;
        }
        D3D11_QUERY_DESC d = { D3D11_QUERY_TIMESTAMP_DISJOINT, 0 };
        dev->lpVtbl->CreateQuery(dev, &d, &q_disjoint);
        d.Query = D3D11_QUERY_TIMESTAMP;
        dev->lpVtbl->CreateQuery(dev, &d, &q_start);
        dev->lpVtbl->CreateQuery(dev, &d, &q_end);
        queries_ready = q_disjoint && q_start && q_end;
        timing_is_gpu = queries_ready;
        if (!queries_ready) {
            return;
        }
    }
    ID3D11DeviceContext *ctx = (ID3D11DeviceContext *)sg_d3d11_device_context();
    ctx->lpVtbl->Begin(ctx, (ID3D11Asynchronous *)q_disjoint);
    ctx->lpVtbl->End(ctx, (ID3D11Asynchronous *)q_start);
}

/* Returns the frame's GPU milliseconds, or a negative number if this frame
   cannot be trusted (the disjoint query reports the clock hiccuped). */
static double clock_end(void) {
    if (!queries_ready) {
        return sapp_frame_duration_unfiltered() * 1000.0;
    }
    ID3D11DeviceContext *ctx = (ID3D11DeviceContext *)sg_d3d11_device_context();
    ctx->lpVtbl->End(ctx, (ID3D11Asynchronous *)q_end);
    ctx->lpVtbl->End(ctx, (ID3D11Asynchronous *)q_disjoint);

    /* Spin until the GPU answers. A benchmark is allowed to block. */
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj;
    while (ctx->lpVtbl->GetData(ctx, (ID3D11Asynchronous *)q_disjoint,
                                &dj, sizeof dj, 0) != S_OK) {
    }
    UINT64 t0 = 0, t1 = 0;
    while (ctx->lpVtbl->GetData(ctx, (ID3D11Asynchronous *)q_start,
                                &t0, sizeof t0, 0) != S_OK) {
    }
    while (ctx->lpVtbl->GetData(ctx, (ID3D11Asynchronous *)q_end,
                                &t1, sizeof t1, 0) != S_OK) {
    }
    if (dj.Disjoint || dj.Frequency == 0) {
        return -1.0;
    }
    return (double)(t1 - t0) * 1000.0 / (double)dj.Frequency;
}
#else
/* No GPU clock here. The frame duration includes the CPU and the present, so
   it overstates the shader and jitters more; the report says so. */
static void clock_begin(void) {
}
static double clock_end(void) {
    return sapp_frame_duration_unfiltered() * 1000.0;
}
#endif

/* ---- the run ------------------------------------------------------------ */

static void before_frame(void) {
    clock_begin();
}

static void report(void) {
    printf("\n  shader: %s\n", shader_path);
    printf("  %dx%d, spectral, %s, median of %d frames\n\n",
           sapp_width(), sapp_height(),
           timing_is_gpu ? "GPU timestamps"
                         : "frame clock (includes CPU and present)",
           want_frames);
    printf("  panels   ms/frame        fps     vs 1 panel\n");
    for (int i = 0; i < stage_count; i++) {
        printf("  %6d   %8.3f   %8.1f   %8.2fx\n",
               stage_rects[i], stage_ms[i], 1000.0 / stage_ms[i],
               stage_ms[i] / stage_ms[0]);
    }
    if (!timing_is_gpu) {
        printf("\n  NOTE: no GPU clock on this backend -- these are frame\n"
               "  times, not shader times. Compare them to each other, not\n"
               "  to numbers from a backend that timestamps.\n");
    }
}

static void after_frame(void) {
    double ms = clock_end();
    frames++;
    if (frames <= WARMUP || ms < 0.0) {
        return;
    }
    if (nsamples < MAX_SAMPLES) {
        samples[nsamples++] = ms;
    }
    if (nsamples < want_frames) {
        return;
    }

    qsort(samples, (size_t)nsamples, sizeof samples[0], cmp_double);
    stage_ms[stage] = samples[nsamples / 2];
    stage++;
    if (stage >= stage_count) {
        report();
        exit(0);
    }
    /* Next panel count, same window and same shader. */
    build_scene(stage_rects[stage]);
    nsamples = 0;
    frames = 0;
}

sapp_desc sokol_main(int argc, char *argv[]) {
    int one_rects = 0;
    shader_path = holo_shader_path();

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--shader") == 0 && i + 1 < argc) {
            shader_path = argv[++i];
        } else if (strcmp(argv[i], "--rects") == 0 && i + 1 < argc) {
            one_rects = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            want_frames = atoi(argv[++i]);
        } else {
            printf("bench: usage: bench [--rects N] [--shader PATH] "
                   "[--frames N]\n");
            exit(2);
        }
    }
    if (want_frames < 1 || want_frames > MAX_SAMPLES) {
        want_frames = 300;
    }

    if (one_rects > 0) {
        if (one_rects > HOLO_MAX_RECTS) {
            printf("bench: --rects above HOLO_MAX_RECTS (%d). Raise it in "
                   "source/cpu_trace.h to sweep further, and mind that the\n"
                   "       shaders carry the slot map by hand.\n",
                   HOLO_MAX_RECTS);
            exit(2);
        }
        stage_rects[stage_count++] = one_rects;
    } else {
        /* 1, 2, 4, 8, ... up to the cap, and the cap itself. */
        for (int n = 1; n < HOLO_MAX_RECTS && stage_count < MAX_STAGES - 1;
             n *= 2) {
            stage_rects[stage_count++] = n;
        }
        stage_rects[stage_count++] = HOLO_MAX_RECTS;
    }

    build_scene(stage_rects[0]);

    FILE *f = fopen(shader_path, "rb");
    if (!f) {
        printf("bench: cannot open %s -- run from the repository root\n",
               shader_path);
        exit(2);
    }
    size_t n = fread(shader_src, 1, sizeof shader_src - 1, f);
    shader_src[n] = 0;
    fclose(f);

    sapp_desc d = holo_display_app(&(HoloDisplayDesc){
        .title = "hologram bench",
        .fs_source = shader_src,
        .uniforms = &gpu,
        .uniforms_size = sizeof gpu,
        .before_frame = before_frame,
        .after_frame = after_frame,
    });
    /* Let the GPU run flat out. swap_interval = 0 will not do it: sokol's
       _sapp_def turns a zero back into 1. disable_vsync is what reaches
       Present. The timestamps do not depend on this; the sample fills
       faster, and the fallback clock depends on it entirely. */
    d.disable_vsync = true;
    return d;
}
