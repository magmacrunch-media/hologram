/* What a scene costs, and what may honestly be said about the number.
 *
 * tools/bench answers "what does a panel cost on the backend you ship" and
 * this cannot: a page runs WebGL2, through a different driver and a
 * different shader compiler, on whatever the browser decided to hand it. So
 * the editor is not a reimplementation of bench and should never be read as
 * one. What it can do, and bench cannot, is measure THIS scene -- the room
 * you are editing, with the panels you actually put in it -- and tell you
 * what an edit just did to it.
 *
 * IT IS NOT BENCH'S MEASUREMENT, AND THE RATIOS DO NOT LINE UP EITHER
 *
 * The obvious thing to do with build/bench.json is put its "vs 1 panel"
 * column beside this one, on the grounds that a ratio should survive the
 * trip between backends where milliseconds do not. That is wrong here, and
 * measuring it is what showed it: bench builds a synthetic scene of N panels
 * and NOTHING ELSE, so its baseline is one panel alone. This ladder
 * truncates the rects of a real room and keeps everything else -- m7_room's
 * three spheres, one of them dispersive glass traced at twelve wavelengths.
 * That fixed cost dominates: the same room measures 8.63 ms spectral against
 * 0.58 ms in RGB, and its panel curve is nearly flat where bench's rises
 * 1.55x over the same range. Two different questions, and the ratios are no
 * more comparable than the milliseconds.
 *
 * So the ladder starts at ZERO panels. That stage is the cost of everything
 * in the room that is not a panel, and every stage above it is reported as
 * what the panels add on top -- which is the number an edit actually moves,
 * and which no amount of comparing against bench would have produced.
 * bench's own figures are shown separately, under their own heading, as the
 * measurement of a different scene on the backend that ships.
 *
 * ON MEASURING IT AT ALL, which tools/bench's header is worth re-reading
 * about: wall-clock frame time is not shader time. The CPU runs ahead of the
 * GPU and you end up timing the loop -- bench reports having measured
 * 0.104 ms for work that took 0.64 ms before it started using timestamp
 * queries. WebGL2 has the same trap and the same escape,
 * EXT_disjoint_timer_query_webgl2, and the panel refuses to report GPU
 * timings without it: with no timer extension it says so and reports frame
 * times under a different name, rather than quietly offering a worse number
 * as though it were the same one.
 */
(function (root) {
    'use strict';

    function median(xs) {
        if (!xs.length) { return 0; }
        var s = xs.slice().sort(function (a, b) { return a - b; });
        return s[s.length >> 1];
    }

    /* The middle half's width: a noise floor that a couple of stray frames
       cannot inflate the way a min-to-max range can. Anything smaller than
       this is not a measurement, and saying so is the only honest thing to
       print -- a difference below the noise comes out NEGATIVE about half
       the time, and a table that reports a panel making the frame cheaper
       has stopped describing the renderer. */
    function spread(xs) {
        if (xs.length < 4) { return Infinity; }
        var s = xs.slice().sort(function (a, b) { return a - b; });
        return s[Math.floor(s.length * 0.75)] - s[Math.floor(s.length * 0.25)];
    }

    /* Zero, then bench's ladder: 1, 2, 4, 8 ... and the count itself.
       Anything past the scene's own panels would be measuring a scene
       nobody has. Zero is the one that makes the rest mean something. */
    function stages(count) {
        var out = [0];
        for (var n = 1; n < count; n *= 2) { out.push(n); }
        if (count >= 1) { out.push(count); }
        return out;
    }

    /* A scene with only its first `n` rects. Everything else is shared by
       reference -- the caller packs it and throws it away. */
    function truncated(doc, n) {
        var copy = {};
        Object.keys(doc).forEach(function (k) { copy[k] = doc[k]; });
        copy.rects = (doc.rects || []).slice(0, n);
        return copy;
    }

    /* What the panels add on top of everything else in the room. The zero
       stage is the floor; each row above it reports its own total and the
       difference, which is the part an edit to the panels can move. */
    function decompose(stages) {
        var zero = stages.filter(function (s) { return s.panels === 0; })[0];
        var fixed = zero ? zero.ms : 0;
        var noise = zero && isFinite(zero.spread) ? zero.spread : 0;
        var rows = stages.filter(function (s) { return s.panels > 0; })
            .map(function (s) {
                var added = s.ms - fixed;
                return {
                    panels: s.panels,
                    ms: s.ms,
                    added: added,
                    perPanel: s.panels ? added / s.panels : 0,
                    /* Not "small": unmeasurable. Below the noise the sign
                       itself is a coin toss. */
                    belowNoise: Math.abs(added) < noise
                };
            });
        return {
            fixed: fixed,
            noise: noise,
            rows: rows,
            /* When even the fullest scene cannot be told from the empty one,
               the panels are not what this frame is spending its time on --
               which is the finding, not a failure of the measurement. */
            allBelowNoise: rows.length > 0 && rows.every(function (r) {
                return r.belowNoise;
            })
        };
    }

    root.cost = {
        median: median, spread: spread, stages: stages, truncated: truncated,
        decompose: decompose
    };
}(window.Hologram = window.Hologram || {}));
