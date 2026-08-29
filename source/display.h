#ifndef HOLO_DISPLAY_H
#define HOLO_DISPLAY_H

/* The window, the GPU device, and the surface the tracer renders through.
 *
 * hologram draws every frame the same way: one fullscreen quad, one fragment
 * shader, uniforms describing the scene and the camera. This module owns that
 * plumbing -- sokol bring-up, the quad pipeline, and the per-frame uniform
 * upload -- so the tracer proper (from M2 on) is just the shader source and
 * the uniform contents.
 *
 * The design resolution follows the games: 640x480, scaled to whatever the
 * window really is. holo_display_uniforms() reports the real pixel size; the
 * shader letterboxes from there.
 */

struct sapp_event;

/* Per-frame uniforms every hologram shader receives, in this layout. Keep it
   16-byte aligned the way constant buffers want; grow it only from the end. */
typedef struct {
    float width;    /* framebuffer size in pixels */
    float height;
    float time;     /* seconds since the window opened */
    float _pad;
} HoloDisplayUniforms;

typedef struct {
    const char *title;       /* window title */
    int         width;       /* initial window size; 0 -> 640x480 */
    int         height;
    const char *fs_source;   /* fragment shader source for the quad (backend
                                dialect; HLSL on Windows until sokol-shdc
                                arrives) */

    /* A game's own uniform block, uploaded every frame in place of the bare
       HoloDisplayUniforms. It must START with a HoloDisplayUniforms --
       display.c fills those fields in each frame before uploading -- and the
       game writes the rest whenever it likes; the upload reads the memory
       fresh every frame. Leave null to get just the built-in block. */
    void *uniforms;
    int   uniforms_size;

    /* Called each frame BEFORE uniforms are read and the quad drawn: the
       place a game runs its simulation and writes the camera into its
       uniform block. */
    void (*before_frame)(void);

    /* Called after each frame is drawn, still inside the frame loop: the
       place to read pixels back, count frames, request quit. */
    void (*after_frame)(void);

    /* Every sokol event, for input. See input.h for the folding. */
    void (*event_cb)(const struct sapp_event *ev);
} HoloDisplayDesc;

/* The tracer's source for the backend this build targets.
 *
 * There is one shader file per dialect -- HLSL for D3D11, GLSL for GL and
 * GLES3, MSL for Metal -- and it is read from disk at startup rather than
 * compiled in, so the tracer can be edited and an example relaunched without
 * recompiling. A game ships the file for its backend beside the binary. */
const char *holo_shader_path(void);

/* Read that file into buf, NUL-terminated. Returns 1, or 0 after printing
   why: no such file (the usual cause is being run from somewhere other than
   the repository root), or a file too large for buf -- which is worth
   catching, because a silently truncated shader fails much later as an
   unexplained compile error. */
int holo_load_shader(char *buf, int buf_size);

/* Fill in sokol's sapp_desc from ours. sokol owns main(), so a game's entry
   point is sokol_main() returning holo_display_app(&desc); the callbacks
   below then run inside the frame loop. */
struct sapp_desc holo_display_app(const HoloDisplayDesc *desc);

/* The frame body: upload uniforms, draw the quad, present. Called by the
   internal frame callback; exposed for games that add their own callbacks. */
void holo_display_frame(void);

/* Seconds since init, as the uniforms will report it. */
float holo_display_time(void);

/* Copy the frame most recently drawn into rgba (w*h*4 bytes, rows top-down),
   which must match the real framebuffer size. Returns 1, or 0 where the
   backend cannot read back (only D3D11 answers today). This exists for the
   oracle: the GPU-vs-CPU image diff needs the GPU's actual pixels. */
int holo_display_read_frame(unsigned char *rgba, int w, int h);

#endif
