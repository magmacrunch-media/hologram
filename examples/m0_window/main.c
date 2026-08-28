/* M0: a window, a fullscreen quad, uniforms arriving every frame.
 *
 * The shader is already shaped like the tracer to come: it reconstructs a ray
 * direction per pixel from the uniforms and shades the sky by it. No scene
 * yet -- the first primitive lands with M1/M2 -- but the frame this draws is
 * the frame every later milestone refines.
 */
#include "../../external/sokol/sokol_app.h"
#include "../../hologram.h"

static const char *FS_HLSL =
    "cbuffer params : register(b0) {\n"
    "    float2 res;\n"
    "    float  time;\n"
    "    float  pad;\n"
    "};\n"
    "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {\n"
    "    /* Per-pixel ray direction: pinhole camera looking down -z. */\n"
    "    float2 p = (uv * 2.0 - 1.0) * float2(res.x / res.y, -1.0);\n"
    "    float3 dir = normalize(float3(p, -1.5));\n"
    "    /* Sky gradient by elevation, drifting slowly so the uniform upload\n"
    "       is visibly alive. */\n"
    "    float t = 0.5 * (dir.y + 1.0);\n"
    "    float3 horizon = float3(1.0, 0.9, 0.8);\n"
    "    float3 zenith  = float3(0.25, 0.45, 0.9 + 0.1 * sin(time * 0.5));\n"
    "    return float4(lerp(horizon, zenith, t), 1.0);\n"
    "}\n";

sapp_desc sokol_main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    return holo_display_app(&(HoloDisplayDesc){
        .title = "hologram m0",
        .fs_source = FS_HLSL,
    });
}
