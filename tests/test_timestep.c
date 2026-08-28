/* The fixed-step accumulator behind clock_fixed_steps().
 *
 * The whole point of a fixed step is that the same wall-clock time always buys
 * the same number of logic steps, however the frames it arrived in were shaped.
 * That is an arithmetic claim, so it belongs in a test rather than in a comment
 * -- and it could not be one while the accumulator sat in clock.c next to
 * gettime().
 *
 *   make test-timestep
 */
#include <stdio.h>
#include "harness.h"
#include "timestep.h"

/* Run n frames of dt seconds each, totalling the steps they owe. */
static int steps_over(int n, float dt) {
    int total = 0;
    for (int i = 0; i < n; i++) {
        timestep_advance(dt);
        total += timestep_steps();
    }
    return total;
}

static void test_off_by_default(void) {
    printf("timestep: off unless a game asks\n");
    timestep_set_hz(0);

    check_int(timestep_hz(), 0, "no rate set");
    check_int(timestep_steps(), 0, "and no steps owed");
    check_int(steps_over(10, 1.0f / 60.0f), 0, "ten frames still owe nothing");
    check(timestep_dt() == 0.0f, "and a step is worth no time");
}

static void test_matching_rate(void) {
    printf("timestep: frames arriving at the step rate\n");
    timestep_set_hz(60);

    check_int(timestep_hz(), 60, "the rate is what was asked for");
    check(timestep_dt() > 0.0166f && timestep_dt() < 0.0167f, "a step is a sixtieth of a second");

    /* The ordinary case: a console holding 60fps, one step per frame. */
    check_int(steps_over(60, 1.0f / 60.0f), 60, "sixty frames at 60Hz owe sixty steps");
}

static void test_time_is_conserved(void) {
    printf("timestep: the same second, cut up differently\n");

    /* One second of real time buys the same steps whether it arrives as clean
       60ths, as ragged halves, or in one lump -- that equivalence is the reason
       to have a fixed step at all. */
    timestep_set_hz(60);
    int even = steps_over(60, 1.0f / 60.0f);

    timestep_set_hz(60);
    int ragged = steps_over(120, 1.0f / 120.0f);

    timestep_set_hz(60);
    int chunky = steps_over(10, 0.1f);

    check_int(even, 60, "sixty even frames");
    check_int(ragged, 60, "a hundred and twenty short ones");
    check_int(chunky, 60, "ten long ones");

    /* A frame shorter than a step owes nothing yet, but the time is kept and
       spent later rather than thrown away -- otherwise a game running above the
       step rate would slowly lose time. */
    timestep_set_hz(60);
    timestep_advance(1.0f / 240.0f);
    check_int(timestep_steps(), 0, "a quarter-step frame owes nothing");
    timestep_advance(1.0f / 240.0f);
    timestep_advance(1.0f / 240.0f);
    check_int(timestep_steps(), 0, "nor do three quarters");
    timestep_advance(1.0f / 240.0f);
    check_int(timestep_steps(), 1, "the fourth quarter pays for a whole step");
}

static void test_stall_is_dropped(void) {
    printf("timestep: a stall, not a slow frame\n");
    timestep_set_hz(60);

    /* Two seconds in one frame is a load, not gameplay. Replaying it would run
       120 steps at once; the cap takes what it can and drops the rest. */
    timestep_advance(2.0f);
    check_int(timestep_steps(), TIMESTEP_MAX_STEPS, "a huge frame owes at most the cap");

    /* And the backlog does not survive to be repaid over the frames after it,
       which is what would make the game sprint once the load finished. */
    timestep_advance(1.0f / 60.0f);
    check_int(timestep_steps(), 1, "the frame after a stall is an ordinary frame");
}

static void test_reset_and_rate_change(void) {
    printf("timestep: resetting and changing rate\n");

    timestep_set_hz(60);
    timestep_advance(1.0f / 120.0f);      /* half a step banked */
    timestep_reset();
    check_int(timestep_steps(), 0, "a reset owes nothing");
    timestep_advance(1.0f / 120.0f);
    check_int(timestep_steps(), 0, "and the banked half-step went with it");

    /* Changing rate mid-run must not carry a remainder measured in the old step
       into the new one. */
    timestep_set_hz(60);
    timestep_advance(1.0f / 120.0f);
    timestep_set_hz(30);
    check_int(timestep_hz(), 30, "the new rate took");
    check_int(timestep_steps(), 0, "and the old remainder did not come with it");
    check_int(steps_over(30, 1.0f / 30.0f), 30, "thirty frames at 30Hz owe thirty steps");

    timestep_set_hz(0);
    check_int(timestep_steps(), 0, "turning it off owes nothing");
    check_int(steps_over(5, 1.0f), 0, "and stays off however long the frames are");
}

static void test_nonsense_deltas(void) {
    printf("timestep: deltas that should not happen\n");
    timestep_set_hz(60);

    /* A clock that goes backwards should cost a frame of motion, not run the
       game in reverse. */
    timestep_advance(-1.0f);
    check_int(timestep_steps(), 0, "a negative delta owes nothing");
    timestep_advance(1.0f / 60.0f);
    check_int(timestep_steps(), 1, "and did not eat the frame after it");

    timestep_set_hz(60);
    timestep_advance(0.0f);
    check_int(timestep_steps(), 0, "a zero delta owes nothing");

    timestep_set_hz(-5);
    check_int(timestep_hz(), 0, "a negative rate reads as off");
}

int main(void) {
    test_off_by_default();
    test_matching_rate();
    test_time_is_conserved();
    test_stall_is_dropped();
    test_reset_and_rate_change();
    test_nonsense_deltas();
    return report();
}
