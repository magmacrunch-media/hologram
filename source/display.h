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
 * window really is. The HoloDisplayUniforms block below carries the real
 * pixel size, refreshed every frame; the shader letterboxes from there.
 */

#include <stdint.h>

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

/* The same, from a path of the caller's choosing. A tool that compares one
   tracer against another has to load a file that is not this build's
   default -- but it still needs the dialect preamble, which belongs to the
   backend rather than to the file. Getting that wrong is invisible on
   D3D11, where the preamble is empty, and fatal on GL, where the shader
   then compiles as GLSL 1.10. */
int holo_load_shader_from(const char *path, char *buf, int buf_size);

/* Fill in sokol's sapp_desc from ours. sokol owns main(), so a game's entry
   point is sokol_main() returning holo_display_app(&desc); the callbacks
   below then run inside the frame loop. */
struct sapp_desc holo_display_app(const HoloDisplayDesc *desc);

/* The frame body: upload uniforms, draw the quad, present. Called by the
   internal frame callback; exposed for games that add their own callbacks. */
void holo_display_frame(void);

/* Seconds since init, as the uniforms will report it. */
float holo_display_time(void);

/* Text on the screen: a bitmap font, positioned in window pixels.
 *
 * Not a GUI toolkit and not a typesetter: eight-pixel glyphs at an integer
 * scale, drawn last in the frame over the traced image, which is enough for a
 * line of keys along the foot of a window and a readout above it, and is
 * exactly what a game on this engine otherwise lacks completely. (A game
 * without it ends up putting its whole interface in the window's title bar.
 * One did.) It wraps sokol_debugtext, the same way daffodil's text.c does and
 * with the same font, and lives here because display.c is the only file that
 * talks to sokol: a game that compiles display.c has text, with no new source
 * file to add to a build script.
 *
 * Queue a frame's text from before_frame; display draws the queue inside its
 * pass after the tracer's quad, and a frame that queued nothing draws nothing
 * and is the frame it always was.
 *
 * THE ORACLE READS THE PRESENTED FRAME, TEXT AND ALL. holo_oracle_diff
 * compares what is on the screen with what the CPU tracer drew, and the CPU
 * tracer draws no text, so a game must queue none on a frame it means to
 * diff. Every example here already has a diff_mode flag that says when. */

/* Start a frame's text at an integer scale: 1 is 8 px glyphs, 2 is 16. */
void holo_text_begin(int scale);

/* Queue a string with its top-left at (px, py), in window pixels from the
   top left. rgba is 0xAABBGGRR, the order sokol_debugtext takes. */
void holo_text_at(float px, float py, uint32_t rgba, const char *s);

/* The same, drawn twice: a dark copy a pixel down and right, then the colour.
   What keeps a line legible over a bright floor and a dark bench alike. */
void holo_text_shadowed(float px, float py, uint32_t rgba, const char *s);

/* Drawn five times: a dark copy at each diagonal, then the colour. A shadow
   fails where what is behind the text is bright on the shadow's own side,
   and a traced floor in full sun is exactly that: pale words over it lose
   their top-left edges. An outline cannot, because the dark is on every
   side. It costs five characters of HOLO_TEXT_CHARS for each one shown, so
   it is for a few lines of heads-up display, not for a page. */
void holo_text_outlined(float px, float py, uint32_t rgba, const char *s);

/* The width one string will take at the current scale, and the height of a
   line, in pixels, for a caller laying things out from the bottom or the
   right. */
float holo_text_width(const char *s);
float holo_text_line_height(void);

/* Copy the frame most recently drawn into rgba (w*h*4 bytes, rows top-down),
   which must match the real framebuffer size. Returns 1, or 0 where the
   backend cannot read back (only D3D11 answers today). This exists for the
   oracle: the GPU-vs-CPU image diff needs the GPU's actual pixels. */
int holo_display_read_frame(unsigned char *rgba, int w, int h);

#endif
