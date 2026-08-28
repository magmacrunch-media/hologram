/* See input.h. Talks to sokol for the event types and mouse lock, so it
 * lives with the display-linked sources, not the pure ones. */
#include "../external/sokol/sokol_app.h"
#include "input.h"

void holo_input_event(HoloInput *in, const struct sapp_event *ev) {
    switch (ev->type) {
    case SAPP_EVENTTYPE_KEY_DOWN:
        if (ev->key_code >= 0 && ev->key_code < 512) {
            in->held[ev->key_code] = 1;
        }
        if (ev->key_code == SAPP_KEYCODE_ESCAPE) {
            sapp_lock_mouse(false);
        }
        break;
    case SAPP_EVENTTYPE_KEY_UP:
        if (ev->key_code >= 0 && ev->key_code < 512) {
            in->held[ev->key_code] = 0;
        }
        break;
    case SAPP_EVENTTYPE_MOUSE_DOWN:
        sapp_lock_mouse(true);
        break;
    case SAPP_EVENTTYPE_MOUSE_MOVE:
        if (sapp_mouse_locked()) {
            in->mouse_dx += ev->mouse_dx;
            in->mouse_dy += ev->mouse_dy;
        }
        break;
    case SAPP_EVENTTYPE_UNFOCUSED:
        /* Keys released while unfocused never send KEY_UP; a stuck walk
           key is worse than a dropped one. */
        for (int i = 0; i < 512; i++) {
            in->held[i] = 0;
        }
        break;
    default:
        break;
    }
}

int holo_input_held(const HoloInput *in, int keycode) {
    return keycode >= 0 && keycode < 512 ? in->held[keycode] : 0;
}

void holo_input_look(HoloInput *in, float *dx, float *dy) {
    *dx = in->mouse_dx;
    *dy = in->mouse_dy;
    in->mouse_dx = 0.0f;
    in->mouse_dy = 0.0f;
}
