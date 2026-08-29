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

#include <stdio.h>
#include <string.h>

/* Which dialect this build needs. The backend macro arrives from the build
   script, the same one that selects sokol's backend, so the path and the
   device can never disagree. */
#if defined(SOKOL_D3D11)
    #define HOLO_SHADER_PATH "shaders/trace.hlsl"
    #define HOLO_SHADER_PREAMBLE ""
#elif defined(SOKOL_METAL)
    #define HOLO_SHADER_PATH "shaders/trace.metal"
    #define HOLO_SHADER_PREAMBLE ""
#elif defined(SOKOL_GLCORE)
    #define HOLO_SHADER_PATH "shaders/trace.glsl"
    #define HOLO_SHADER_PREAMBLE "#version 410\n"
#elif defined(SOKOL_GLES3)
    /* One file serves both GL profiles: only the version line and the
       precision defaults differ, so they are prepended rather than
       duplicated into a second copy of the tracer that would then have to
       be kept in step with the first. */
    #define HOLO_SHADER_PATH "shaders/trace.glsl"
    #define HOLO_SHADER_PREAMBLE "#version 300 es\nprecision highp float;\nprecision highp int;\n"
#else
    #error "hologram: no tracer dialect for this sokol backend"
#endif

const char *holo_shader_path(void) {
    return HOLO_SHADER_PATH;
}

int holo_load_shader_from(const char *path, char *buf, int buf_size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("could not open %s -- run from the repository root\n",
               path);
        return 0;
    }
    /* The preamble goes in first, so the tracer file itself stays
       dialect-clean: no version line to get wrong when hand-editing. */
    size_t pre = strlen(HOLO_SHADER_PREAMBLE);
    if ((int)pre >= buf_size) {
        fclose(f);
        return 0;
    }
    memcpy(buf, HOLO_SHADER_PREAMBLE, pre);
    size_t n = fread(buf + pre, 1, (size_t)buf_size - pre - 1, f);
    buf[pre + n] = 0;
    /* Filling the buffer without reaching the end means the rest was lost. */
    int truncated = !feof(f);
    fclose(f);
    if (truncated) {
        printf("%s does not fit in %d bytes\n", path, buf_size);
        return 0;
    }
    return 1;
}

int holo_load_shader(char *buf, int buf_size) {
    return holo_load_shader_from(HOLO_SHADER_PATH, buf, buf_size);
}

/* One fullscreen triangle from the vertex id -- no vertex buffer, nothing to
   bind. uv runs 0..1 across the visible frame with (0,0) at the TOP-left,
   which is the corner the tracer's camera treats as the top of the image.

   The same clip-space expression serves every dialect: +y is up in D3D,
   GL and Metal clip space alike, so the -2/+1 flip lands uv=(0,0) at the
   top-left in all three. Only the spelling changes. */
#if defined(SOKOL_D3D11)
static const char *VS_SOURCE =
    "struct vs_out { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
    "vs_out main(uint vid : SV_VertexID) {\n"
    "    vs_out o;\n"
    "    float2 grid = float2((vid << 1) & 2, vid & 2);\n"
    "    o.uv  = grid;\n"
    "    o.pos = float4(grid * float2(2, -2) + float2(-1, 1), 0, 1);\n"
    "    return o;\n"
    "}\n";
#elif defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)
/* The version line is prepended by holo_load_shader for the fragment stage;
   the vertex stage carries its own, since it is compiled in. */
static const char *VS_SOURCE =
    HOLO_SHADER_PREAMBLE
    "out vec2 uv;\n"
    "void main() {\n"
    "    vec2 grid = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
    "    uv  = grid;\n"
    "    gl_Position = vec4(grid * vec2(2.0, -2.0) + vec2(-1.0, 1.0), 0.0, 1.0);\n"
    "}\n";
#elif defined(SOKOL_METAL)
static const char *VS_SOURCE =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct vs_out { float4 pos [[position]]; float2 uv [[user(locn0)]]; };\n"
    "vertex vs_out main0(uint vid [[vertex_id]]) {\n"
    "    vs_out o;\n"
    "    float2 grid = float2((vid << 1) & 2, vid & 2);\n"
    "    o.uv  = grid;\n"
    "    o.pos = float4(grid * float2(2, -2) + float2(-1, 1), 0, 1);\n"
    "    return o;\n"
    "}\n";
#endif

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

    /* The block the fragment stage receives: the game's, when it supplies
       one, and the bare header otherwise. Must be a whole number of float4
       slots -- the GL path below describes it as an array of them. */
    const size_t ub_size = state.desc.uniforms
                               ? (size_t)state.desc.uniforms_size
                               : sizeof(HoloDisplayUniforms);

    sg_shader_desc sd = {
        .vertex_func.source = VS_SOURCE,
        .fragment_func.source = state.desc.fs_source,
        .uniform_blocks[0] = {
            .stage = SG_SHADERSTAGE_FRAGMENT,
            .size = (uint32_t)ub_size,
        },
        .label = "holo-quad-shader",
    };

#if defined(SOKOL_D3D11)
    /* Shader model 5.0, not sokol's 4.0 default: the tracer dynamically
       indexes cbuffer arrays (scene lookups) AND large local arrays (the
       ray stack), and SM4 has no native dynamic cbuffer indexing -- fxc
       emulates it by copying arrays into indexable temps, and past a
       certain count that emulation silently aliases the copies onto other
       arrays. SM5 indexes constant buffers in hardware. */
    sd.vertex_func.d3d11_target = "vs_5_0";
    sd.fragment_func.d3d11_target = "ps_5_0";
    sd.uniform_blocks[0].hlsl_register_b_n = 0;
#elif defined(SOKOL_METAL)
    /* MSL has no main(); each stage is its own library, so both may use the
       same entry name. sokol asserts that one is supplied. Being separate
       libraries is also why the varying carries [[user(locn0)]] on both
       sides: across libraries Metal matches by attribute, not name. */
    sd.vertex_func.entry = "main0";
    sd.fragment_func.entry = "main0";
    sd.uniform_blocks[0].msl_buffer_n = 0;
#elif defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)
    /* sokol's GL backend has no uniform buffer objects: it flattens a block
       into individual glUniform*fv calls against named uniforms, and it
       accepts at most SG_MAX_UNIFORMBLOCK_MEMBERS (16) of them. The tracer's
       block has closer to thirty fields, so the GLSL tracer declares the
       whole block as ONE array -- uniform vec4 params[N] -- and reads its
       fields by slot. That is not a workaround so much as the layout that
       was already there: gpu_scene.h counts the block in float4s on both
       sides, and the slot number IS that count. */
    sd.uniform_blocks[0].layout = SG_UNIFORMLAYOUT_STD140;
    sd.uniform_blocks[0].glsl_uniforms[0] = (sg_glsl_shader_uniform){
        .type = SG_UNIFORMTYPE_FLOAT4,
        .array_count = (uint16_t)(ub_size / 16),
        .glsl_name = "params",
    };
#endif

    sg_shader shd = sg_make_shader(&sd);

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
#elif (defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)) && !defined(_WIN32)
/* glReadPixels reads the default framebuffer bottom-up, while the oracle and
   the CPU tracer it compares against both count rows from the top, so the
   rows are reversed on the way out.

   Not available on Windows+GL: there sokol supplies its own GL loader and
   suppresses the system GL headers, and glReadPixels is not among the
   entry points it loads. Windows traces through D3D11 above in any case. */
int holo_display_read_frame(unsigned char *rgba, int w, int h) {
    if (w != sapp_width() || h != sapp_height()) {
        return 0;
    }
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    for (int y = 0; y < h / 2; y++) {
        unsigned char *a = rgba + (size_t)y * w * 4;
        unsigned char *b = rgba + (size_t)(h - 1 - y) * w * 4;
        for (int i = 0; i < w * 4; i++) {
            unsigned char t = a[i];
            a[i] = b[i];
            b[i] = t;
        }
    }
    return 1;
}
#elif defined(SOKOL_METAL)
/* Blit the drawable into a shared-storage staging texture, wait, and read it.
   A drawable's own texture is private storage, so getBytes cannot touch it
   directly -- the copy is what makes it host-visible.

   This runs from after_frame, which display.c calls once sg_commit() has
   handed the frame's command buffer to sokol's queue but before sokol_app
   presents, so the drawable still holds the frame just drawn.

   The blit goes on a queue of our own, because sokol exposes its device but
   not its command queue, and Metal orders work within a queue rather than
   across queues. That is a real gap in the general case. It is harmless for
   the only caller: the oracle diffs a still scene several frames in, so
   every frame in flight carries identical pixels, and capturing the previous
   one instead of the current one is a difference without a difference. Do
   not reach for this to read back a moving frame -- it would need sokol's
   own queue, or a completion handler on its command buffer.

   Objective-C, because on macOS this translation unit is compiled as such
   (build.sh passes -x objective-c -fobjc-arc for exactly this). */
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

static id<MTLCommandQueue> holo_mtl_queue = nil;

int holo_display_read_frame(unsigned char *rgba, int w, int h) {
    if (w != sapp_width() || h != sapp_height()) {
        return 0;
    }
    id<MTLDevice> dev = (__bridge id<MTLDevice>)sg_mtl_device();
    id<CAMetalDrawable> drawable =
        (__bridge id<CAMetalDrawable>)sapp_metal_get_current_drawable();
    if (dev == nil || drawable == nil) {
        return 0;
    }
    id<MTLTexture> src = drawable.texture;
    if (src == nil || (int)src.width != w || (int)src.height != h) {
        return 0;
    }
    if (holo_mtl_queue == nil) {
        holo_mtl_queue = [dev newCommandQueue];
        if (holo_mtl_queue == nil) {
            return 0;
        }
    }

    MTLTextureDescriptor *desc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:src.pixelFormat
                                     width:(NSUInteger)w
                                    height:(NSUInteger)h
                                 mipmapped:NO];
    desc.storageMode = MTLStorageModeShared;
    desc.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> staging = [dev newTextureWithDescriptor:desc];
    if (staging == nil) {
        return 0;
    }

    id<MTLCommandBuffer> cb = [holo_mtl_queue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
    [blit copyFromTexture:src
              sourceSlice:0
              sourceLevel:0
             sourceOrigin:MTLOriginMake(0, 0, 0)
               sourceSize:MTLSizeMake((NSUInteger)w, (NSUInteger)h, 1)
                toTexture:staging
         destinationSlice:0
         destinationLevel:0
        destinationOrigin:MTLOriginMake(0, 0, 0)];
    [blit endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    if (cb.status != MTLCommandBufferStatusCompleted) {
        return 0;
    }

    [staging getBytes:rgba
          bytesPerRow:(NSUInteger)w * 4
           fromRegion:MTLRegionMake2D(0, 0, (NSUInteger)w, (NSUInteger)h)
          mipmapLevel:0];

    /* The swapchain is BGRA8, as it is under D3D11; the caller wants RGBA. */
    if (src.pixelFormat == MTLPixelFormatBGRA8Unorm ||
        src.pixelFormat == MTLPixelFormatBGRA8Unorm_sRGB) {
        for (int i = 0; i < w * h; i++) {
            unsigned char b = rgba[4 * i + 0];
            rgba[4 * i + 0] = rgba[4 * i + 2];
            rgba[4 * i + 2] = b;
        }
    }
    return 1;
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
