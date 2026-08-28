#ifndef HOLO_INPUT_H
#define HOLO_INPUT_H

/* Keyboard and mouse, folded to what a first-person game asks each frame:
 * which keys are held, and how far the mouse moved since it was last
 * asked. The event decode is sokol's; this module is the bookkeeping in
 * between, plus mouse capture (click to look, Escape to let go).
 */

struct sapp_event;

typedef struct {
    unsigned char held[512];       /* indexed by sapp_keycode */
    float mouse_dx, mouse_dy;      /* accumulated; holo_input_look consumes */
} HoloInput;

/* Feed every event from the display's event callback through this. Handles
   capture itself: left click locks the mouse for looking, Escape unlocks. */
void holo_input_event(HoloInput *in, const struct sapp_event *ev);

/* Is this sapp_keycode held right now? */
int holo_input_held(const HoloInput *in, int keycode);

/* The mouse movement since the last call, then zero -- call once per
   frame. Returns nothing while the mouse is not captured. */
void holo_input_look(HoloInput *in, float *dx, float *dy);

#endif
