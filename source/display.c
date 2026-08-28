/* See display.h for what this module is. The only file that talks to sokol.
 *
 * The sokol implementations are compiled here (SOKOL_IMPL) so games get them
 * by compiling the engine's sources, magnolia-style, with no library step.
 * The backend macro (SOKOL_D3D11 on Windows) comes from the build script.
 */
#define SOKOL_IMPL
#include "../external/sokol/sokol_app.h"
#include "../external/sokol/sokol_gfx.h"
#include "../external/sokol/sokol_log.h"
#include "../external/sokol/sokol_glue.h"

#include "display.h"

/* One fullscreen triangle from the vertex id -- no vertex buffer, nothing to
   bind. uv runs 0..1 across the visible frame (y down, matching D3D). */
static const char *VS_HLSL =
    "struct vs_out { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
    "vs_out main(uint vid : SV_VertexID) {\n"
    "    vs_out o;\n"
    "    float2 grid = float2((vid << 1) & 2, vid & 2);\n"
    "    o.uv  = grid;\n"
    "    o.pos = float4(grid * float2(2, -2) + float2(-1, 1), 0, 1);\n"
    "    return o;\n"
    "}\n";

static struct {
    HoloDisplayDesc desc;
    sg_pipeline pip;
    double time;
} state;

static void init_cb(void) {
    sg_setup(&(sg_desc){
        .environment = sglue_environment(),
        .logger.func = slog_func,
    });

    sg_shader shd = sg_make_shader(&(sg_shader_desc){
        .vertex_func.source = VS_HLSL,
        .fragment_func.source = state.desc.fs_source,
        .uniform_blocks[0] = {
            .stage = SG_SHADERSTAGE_FRAGMENT,
            .size = state.desc.uniforms
                        ? (size_t)state.desc.uniforms_size
                        : sizeof(HoloDisplayUniforms),
            .hlsl_register_b_n = 0,
        },
        .label = "holo-quad-shader",
    });

    state.pip = sg_make_pipeline(&(sg_pipeline_desc){
        .shader = shd,
        .label = "holo-quad-pipeline",
    });
}

void holo_display_frame(void) {
    /* Frame-duration accumulation rather than a wall clock: what the shader
       calls "time" should advance only when frames do. */
    state.time += sapp_frame_duration();

    if (state.desc.before_frame) {
        state.desc.before_frame();
    }

    HoloDisplayUniforms header = {
        .width  = (float)sapp_width(),
        .height = (float)sapp_height(),
        .time   = (float)state.time,
    };

    sg_range ub;
    if (state.desc.uniforms) {
        /* The game's block starts with our header by contract; fill it in
           and upload the whole thing as the game currently has it. */
        *(HoloDisplayUniforms *)state.desc.uniforms = header;
        ub = (sg_range){ .ptr = state.desc.uniforms,
                         .size = (size_t)state.desc.uniforms_size };
    } else {
        ub = SG_RANGE(header);
    }

    sg_begin_pass(&(sg_pass){ .swapchain = sglue_swapchain() });
    sg_apply_pipeline(state.pip);
    sg_apply_uniforms(0, &ub);
    sg_draw(0, 3, 1);
    sg_end_pass();
    sg_commit();

    if (state.desc.after_frame) {
        state.desc.after_frame();
    }
}

float holo_display_time(void) {
    return (float)state.time;
}

#if defined(SOKOL_D3D11)
/* Read the swapchain's last presented frame through a staging texture. The
   swapchain is BGRA8; the caller gets RGBA. Slow and synchronizing -- which
   is fine, because the only caller is the oracle's image diff.

   Plain lpVtbl COM calls: sokol's headers do not define COBJMACROS, and this
   is the one place in the engine that talks to D3D11 by hand. The texture
   IID is spelled out for the same reason -- it saves linking dxguid for a
   single constant. */
static const GUID HOLO_IID_ID3D11Texture2D =
    { 0x6f15aaf2, 0xd208, 0x4e89,
      { 0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c } };

int holo_display_read_frame(unsigned char *rgba, int w, int h) {
    if (w != sapp_width() || h != sapp_height()) {
        return 0;
    }
    ID3D11Device *dev = (ID3D11Device *)sg_d3d11_device();
    ID3D11DeviceContext *ctx = (ID3D11DeviceContext *)sg_d3d11_device_context();
    IDXGISwapChain *swap = (IDXGISwapChain *)sapp_d3d11_get_swap_chain();
    if (!dev || !ctx || !swap) {
        return 0;
    }

    ID3D11Texture2D *backbuf = 0;
    if (FAILED(swap->lpVtbl->GetBuffer(swap, 0, &HOLO_IID_ID3D11Texture2D,
                                       (void **)&backbuf))) {
        return 0;
    }

    D3D11_TEXTURE2D_DESC desc;
    backbuf->lpVtbl->GetDesc(backbuf, &desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;

    ID3D11Texture2D *staging = 0;
    if (FAILED(dev->lpVtbl->CreateTexture2D(dev, &desc, 0, &staging))) {
        backbuf->lpVtbl->Release(backbuf);
        return 0;
    }
    ctx->lpVtbl->CopyResource(ctx, (ID3D11Resource *)staging,
                              (ID3D11Resource *)backbuf);

    D3D11_MAPPED_SUBRESOURCE mapped;
    int ok = 0;
    if (SUCCEEDED(ctx->lpVtbl->Map(ctx, (ID3D11Resource *)staging, 0,
                                   D3D11_MAP_READ, 0, &mapped))) {
        for (int y = 0; y < h; y++) {
            const unsigned char *src = (const unsigned char *)mapped.pData
                                     + (size_t)y * mapped.RowPitch;
            unsigned char *dst = rgba + (size_t)y * w * 4;
            for (int x = 0; x < w; x++) {
                dst[4 * x + 0] = src[4 * x + 2];   /* B <-> R */
                dst[4 * x + 1] = src[4 * x + 1];
                dst[4 * x + 2] = src[4 * x + 0];
                dst[4 * x + 3] = src[4 * x + 3];
            }
        }
        ctx->lpVtbl->Unmap(ctx, (ID3D11Resource *)staging, 0);
        ok = 1;
    }
    staging->lpVtbl->Release(staging);
    backbuf->lpVtbl->Release(backbuf);
    return ok;
}
#else
int holo_display_read_frame(unsigned char *rgba, int w, int h) {
    (void)rgba; (void)w; (void)h;
    return 0;
}
#endif

static void cleanup_cb(void) {
    sg_shutdown();
}

static void event_cb(const sapp_event *ev) {
    if (state.desc.event_cb) {
        state.desc.event_cb(ev);
    }
}

struct sapp_desc holo_display_app(const HoloDisplayDesc *desc) {
    state.desc = *desc;
    return (sapp_desc){
        .init_cb = init_cb,
        .frame_cb = holo_display_frame,
        .cleanup_cb = cleanup_cb,
        .event_cb = event_cb,
        .width  = desc->width  ? desc->width  : 640,
        .height = desc->height ? desc->height : 480,
        .window_title = desc->title ? desc->title : "hologram",
        .logger.func = slog_func,
    };
}
