/* M2: the tracer on the GPU, held to the oracle.
 *
 * The same scene as the CPU renders, traced by shaders/trace.hlsl on the
 * fullscreen quad. Run bare it is just a window showing the frame; run as
 *
 *   build\m2_gpu.exe --diff
 *
 * it renders a few frames, reads the GPU's pixels back, renders the same
 * frame through cpu_trace.c, and compares. The exit code is the verdict,
 * so build.bat test can hold the GPU to the oracle mechanically. The two
 * images are float twins, not bit twins -- drivers reassociate math -- so
 * the bar is: mean error under 1/255, and fewer than 0.5% of pixels off by
 * more than 8/255 (silhouette pixels, where a grazing ray hits on one side
 * and misses on the other).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../external/sokol/sokol_app.h"
#include "../../hologram.h"

/* Field for field, float4 for float4, the cbuffer in shaders/trace.hlsl. */
typedef struct {
    HoloDisplayUniforms display;
    float cam_pos[3];   float tan_half_fov;
    float cam_fwd[3];   float pad_a;
    float cam_right[3]; float sphere_count;
    float cam_up[3];    float has_floor;
    float sun_dir[3];   float floor_y;
    float horizon[3];   float pad1;
    float zenith[3];    float pad2;
    float floor_a[3];   float pad3;
    float floor_b[3];   float pad4;
    float sph_center_radius[HOLO_MAX_SPHERES][4];
    float sph_albedo[HOLO_MAX_SPHERES][4];
} M2Uniforms;

static M2Uniforms uniforms;
static HoloScene scene;
static char shader_src[16384];
static int diff_mode;
static int frames_drawn;

static void put3(float dst[3], HoloV3 v) { dst[0] = v.x; dst[1] = v.y; dst[2] = v.z; }

static void fill_uniforms(const HoloScene *s, const HoloCamera *cam) {
    put3(uniforms.cam_pos, cam->pos);
    uniforms.tan_half_fov = cam->tan_half_fov;
    put3(uniforms.cam_fwd, cam->forward);
    put3(uniforms.cam_right, cam->right);
    put3(uniforms.cam_up, cam->up);
    uniforms.sphere_count = (float)s->sphere_count;
    uniforms.has_floor = s->has_floor ? 1.0f : 0.0f;
    put3(uniforms.sun_dir, s->sun_dir);
    uniforms.floor_y = s->floor_y;
    put3(uniforms.horizon, s->horizon);
    put3(uniforms.zenith, s->zenith);
    put3(uniforms.floor_a, s->floor_a);
    put3(uniforms.floor_b, s->floor_b);
    for (int i = 0; i < s->sphere_count; i++) {
        put3(uniforms.sph_center_radius[i], s->spheres[i].center);
        uniforms.sph_center_radius[i][3] = s->spheres[i].radius;
        put3(uniforms.sph_albedo[i], s->spheres[i].albedo);
    }
}

static void run_diff(void) {
    const int w = sapp_width(), h = sapp_height();

    unsigned char *gpu = malloc((size_t)w * h * 4);
    if (!holo_display_read_frame(gpu, w, h)) {
        printf("DIFF FAIL: backend cannot read pixels back\n");
        exit(2);
    }

    float *rgb = malloc((size_t)w * h * 3 * sizeof(float));
    HoloCamera cam = holo_camera_make(
        hv3(0, 1.6f, 6), hv3(0, 0.8f, 0), hv3(0, 1, 0),
        55.0f, (float)w / (float)h);
    holo_trace_image(&scene, &cam, w, h, rgb);

    long long sum = 0;
    int outliers = 0, max_diff = 0;
    for (int i = 0; i < w * h; i++) {
        for (int c = 0; c < 3; c++) {
            float v = rgb[3 * i + c];
            v = v < 0 ? 0 : v > 1 ? 1 : v;
            int want = (int)(v * 255.0f + 0.5f);
            int got = gpu[4 * i + c];
            int d = got > want ? got - want : want - got;
            sum += d;
            if (d > max_diff) max_diff = d;
            if (d > 8) { outliers++; break; }
        }
    }
    double mean = (double)sum / ((double)w * h * 3);
    double outlier_pct = 100.0 * outliers / ((double)w * h);
    int ok = mean < 1.0 && outlier_pct < 0.5;

    printf("DIFF %s: %dx%d, mean err %.4f/255, max %d/255, "
           "%.3f%% pixels off by >8\n",
           ok ? "OK" : "FAIL", w, h, mean, max_diff, outlier_pct);
    /* exit() rather than a polite quit: the verdict IS the program, and the
       device teardown we skip matters to nobody at process end. */
    exit(ok ? 0 : 1);
}

static void after_frame(void) {
    frames_drawn++;
    /* A few frames in, so the swapchain is past its first-present wrinkles. */
    if (diff_mode && frames_drawn == 5) {
        run_diff();
    }
}

sapp_desc sokol_main(int argc, char *argv[]) {
    diff_mode = argc > 1 && strcmp(argv[1], "--diff") == 0;

    scene = (HoloScene){
        .spheres = {
            { .center = { 0.0f, 1.0f, 0.0f },  .radius = 1.0f,
              .albedo = { 0.85f, 0.25f, 0.35f } },
            { .center = { -2.2f, 0.6f, 1.0f }, .radius = 0.6f,
              .albedo = { 0.25f, 0.55f, 0.85f } },
            { .center = { 1.9f, 0.45f, 1.4f }, .radius = 0.45f,
              .albedo = { 0.9f, 0.75f, 0.25f } },
        },
        .sphere_count = 3,
        .has_floor = 1,
        .floor_y = 0.0f,
        .floor_a = { 0.85f, 0.85f, 0.85f },
        .floor_b = { 0.25f, 0.3f, 0.35f },
        .sun_dir = { -3.0f / 7, 6.0f / 7, 2.0f / 7 },
        .horizon = { 1.0f, 0.9f, 0.8f },
        .zenith  = { 0.25f, 0.45f, 0.9f },
    };
    /* The aspect only matters through res.x/res.y, which the shader derives
       itself; the basis and fov are what the uniforms carry. */
    HoloCamera cam = holo_camera_make(hv3(0, 1.6f, 6), hv3(0, 0.8f, 0),
                                      hv3(0, 1, 0), 55.0f, 1.0f);
    fill_uniforms(&scene, &cam);

    FILE *f = fopen("shaders\\trace.hlsl", "rb");
    if (!f) {
        printf("could not open shaders\\trace.hlsl -- run from the repo root\n");
        exit(2);
    }
    size_t n = fread(shader_src, 1, sizeof shader_src - 1, f);
    shader_src[n] = 0;
    fclose(f);

    return holo_display_app(&(HoloDisplayDesc){
        .title = "hologram m2",
        .fs_source = shader_src,
        .uniforms = &uniforms,
        .uniforms_size = sizeof uniforms,
        .after_frame = after_frame,
    });
}
