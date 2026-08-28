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
            .size = sizeof(HoloDisplayUniforms),
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

    const HoloDisplayUniforms ub = {
        .width  = (float)sapp_width(),
        .height = (float)sapp_height(),
        .time   = (float)state.time,
    };

    sg_begin_pass(&(sg_pass){ .swapchain = sglue_swapchain() });
    sg_apply_pipeline(state.pip);
    sg_apply_uniforms(0, &SG_RANGE(ub));
    sg_draw(0, 3, 1);
    sg_end_pass();
    sg_commit();
}

float holo_display_time(void) {
    return (float)state.time;
}

static void cleanup_cb(void) {
    sg_shutdown();
}

struct sapp_desc holo_display_app(const HoloDisplayDesc *desc) {
    state.desc = *desc;
    return (sapp_desc){
        .init_cb = init_cb,
        .frame_cb = holo_display_frame,
        .cleanup_cb = cleanup_cb,
        .width  = desc->width  ? desc->width  : 640,
        .height = desc->height ? desc->height : 480,
        .window_title = desc->title ? desc->title : "hologram",
        .logger.func = slog_func,
    };
}
