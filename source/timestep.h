#ifndef TIMESTEP_H
#define TIMESTEP_H

/* The fixed-step accumulator behind clock_fixed_steps().
 *
 * Separated from clock.c for the same reason input_state.c was separated from
 * input.c: everything here is arithmetic, and everything in clock.c is a call to
 * gettime(). Keeping them in one file would leave the counting untestable, and
 * the counting is the part that decides whether a game runs at the same speed on
 * a stalled frame as on a clean one.
 *
 * Nothing outside the engine and its tests should include this -- games call
 * clock.h. It is a seam, not a second public API.
 */

/* A frame that owed more steps than this was a stall, not a slow frame. The
   remaining backlog is dropped rather than replayed: catching up on two seconds
   of hitstun by running two seconds of it at once is not catching up, it is
   teleporting, and it is what makes a game speed up after a load. clock.c
   already refuses to report a delta above 0.2s for the same reason, so at any
   sane rate this cap is the second line of defence rather than the first. */
#define TIMESTEP_MAX_STEPS 16

/* hz <= 0 turns fixed stepping off, which is the default: a game that never asks
   for it is unaffected, and clock_dt() goes on meaning what it always did. */
void  timestep_set_hz(int hz);
int   timestep_hz(void);

/* Seconds in one step, or 0 when disabled. This is the number a fixed-step game
   should be integrating with -- not clock_dt(), which is however long the frame
   really took. */
float timestep_dt(void);

/* Add a frame's real elapsed time and work out how many steps it owes. Called
   once per frame from clock_tick(); a test calls it directly. */
void  timestep_advance(float dt);

/* Steps owed by the frame most recently advanced. */
int   timestep_steps(void);

/* Forgets the accumulated remainder and the step count, leaving the rate alone.
   Called by clock_reset(): time before a reset should not owe steps after it. */
void  timestep_reset(void);

#endif
