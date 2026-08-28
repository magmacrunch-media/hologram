#include "timestep.h"

static int   hz = 0;
static float accum = 0.0f;
static int   steps = 0;

void timestep_set_hz(int rate) {
    hz = rate > 0 ? rate : 0;
    /* Changing the rate mid-run would otherwise carry a remainder measured in
       the old step across to the new one, which is a fraction of a frame of
       drift at exactly the moment a game is least expecting it. */
    accum = 0.0f;
    steps = 0;
}

int timestep_hz(void) { return hz; }

float timestep_dt(void) {
    return hz > 0 ? 1.0f / (float)hz : 0.0f;
}

void timestep_advance(float dt) {
    if (hz <= 0) {
        steps = 0;
        return;
    }

    /* A negative delta means the clock went backwards; adding it would run the
       game in reverse for a frame. Ignore the measurement, keep the remainder. */
    if (dt > 0.0f) accum += dt;

    float step = 1.0f / (float)hz;
    int n = 0;
    while (accum >= step && n < TIMESTEP_MAX_STEPS) {
        accum -= step;
        n++;
    }

    /* Still owing a step after the cap means this frame was a stall. Drop what
       is left rather than carrying it: the debt would otherwise be repaid over
       the following frames, which is a game running fast to catch up. */
    if (accum >= step) accum = 0.0f;

    steps = n;
}

int timestep_steps(void) { return steps; }

void timestep_reset(void) {
    accum = 0.0f;
    steps = 0;
}
