/* source/timestep.c, in JavaScript.
 *
 * The fixed-step accumulator the engine drives its walker with, ported so
 * that walking in the editor is stepped the way walking in a game is: the
 * same rate, the same backlog cap, the same decision to drop a stall rather
 * than repay it. Simulating at frame rate instead would let the editor
 * disagree with the game about whether you fit through a gap, which is the
 * one question walking here is meant to answer.
 *
 * timestep.h says nothing outside the engine and its tests should include
 * it -- "It is a seam, not a second public API." That is about C code
 * linking against the engine, and it is the right rule: a game gets its
 * timestep from the engine. This is not a game and cannot link, and the
 * alternative to porting the seam is inventing a different one.
 *
 * Module state, like the C's file-scope statics, so the shape matches.
 */
(function (root) {
    'use strict';

    var f = Math.fround;

    var MAX_STEPS = 16;

    var hz = 0;
    var accum = 0;
    var steps = 0;

    function setHz(rate) {
        hz = rate > 0 ? rate : 0;
        /* Changing the rate mid-run would otherwise carry a remainder
           measured in the old step across to the new one. */
        accum = 0;
        steps = 0;
    }

    function getHz() { return hz; }

    function dt() { return hz > 0 ? f(1 / hz) : 0; }

    function advance(delta) {
        if (hz <= 0) {
            steps = 0;
            return;
        }
        /* A negative delta means the clock went backwards; adding it would
           run the game in reverse for a frame. Ignore the measurement, keep
           the remainder. */
        if (delta > 0) { accum = f(accum + delta); }

        var stepLen = f(1 / hz);
        var n = 0;
        while (accum >= stepLen && n < MAX_STEPS) {
            accum = f(accum - stepLen);
            n++;
        }
        /* Still owing a step after the cap means this frame was a stall.
           Drop what is left rather than carrying it: the debt would be
           repaid over the following frames, which is a game running fast to
           catch up. */
        if (accum >= stepLen) { accum = 0; }

        steps = n;
    }

    function getSteps() { return steps; }

    function reset() {
        accum = 0;
        steps = 0;
    }

    root.timestep = {
        MAX_STEPS: MAX_STEPS,
        setHz: setHz, hz: getHz, dt: dt,
        advance: advance, steps: getSteps, reset: reset
    };
}(window.Hologram = window.Hologram || {}));
