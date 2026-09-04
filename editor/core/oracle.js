/* holo_oracle_diff's arithmetic, in JavaScript.
 *
 * Holds the GLSL tracer to the CPU reference the same way source/oracle.c
 * holds the D3D11 one: render the frame, compare against an image
 * cpu_trace.c produced, and apply the same two bars -- mean error under
 * 1/255, and under 0.75% of pixels off by more than 8/255.
 *
 * This is what tools/gldiff/gldiff.html does, and the editor supersedes it:
 * same WebGL2 arrangement, same reference files, but no second page and no
 * second server, and the scene is already loaded.
 *
 * ---------------------------------------------------------------------
 * A divergence worth knowing about
 *
 * oracle.c and gldiff.html do NOT compute the same mean, though gldiff's
 * header says it uses "oracle.c's own arithmetic and its own bars".
 *
 * oracle.c breaks out of the channel loop as soon as a channel is off by
 * more than 8:
 *
 *     sum += d;
 *     if (d > 8) { outliers++; break; }
 *
 * so for an outlier pixel the channels after the offending one never reach
 * the sum. gldiff.html sets a flag and keeps summing all three. The two
 * agree exactly on a clean frame, and drift apart in proportion to the
 * outlier count -- which is to say they disagree precisely when a frame is
 * failing and the number matters most.
 *
 * The break is probably meant to stop one pixel being counted as three
 * outliers, which it does; truncating the sum looks like a side effect
 * rather than an intention. But oracle.c is the authority -- it is what
 * gates every example and what the README's pass table was measured with --
 * so its number is the verdict here, and gldiff's is computed alongside and
 * reported when the two differ. Reconciling them is an engine decision, not
 * an editor one.
 */
(function (root) {
    'use strict';

    /* oracle.c's bars. The mean is the load-bearing one; the outlier
       allowance exists because silhouettes and near-critical-angle glass are
       razor edges where the two sides legitimately flip independent float
       coins. */
    var MEAN_BAR = 1.0;
    var OUTLIER_PCT_BAR = 0.75;

    /* build/<name>_ref.bin: two int32 of width and height, then w*h*3 bytes
       already sRGB-encoded by oracle.c's encode_u8 -- so nothing here has to
       reproduce the tone mapping, only the comparison. */
    function parseRef(buffer) {
        var head = new DataView(buffer);
        var w = head.getInt32(0, true), h = head.getInt32(4, true);
        if (w <= 0 || h <= 0 || buffer.byteLength !== 8 + w * h * 3) {
            throw new Error('malformed ref.bin (' + w + 'x' + h + ', ' +
                            buffer.byteLength + ' bytes)');
        }
        return { width: w, height: h, rgb: new Uint8Array(buffer, 8) };
    }

    /* `gpu` is RGBA rows top-down, as view.readFrame() returns it; `ref` is
       parseRef's result. Returns the verdict plus the three images the panel
       draws, so the caller never touches pixels itself. */
    function diff(gpu, ref) {
        var w = ref.width, h = ref.height;
        if (gpu.width !== w || gpu.height !== h) {
            throw new Error('frame is ' + gpu.width + 'x' + gpu.height +
                            ' but the reference is ' + w + 'x' + h);
        }
        var g = gpu.rgba, r = ref.rgb;
        var n = w * h;

        var gpuImg = new Uint8ClampedArray(n * 4);
        var cpuImg = new Uint8ClampedArray(n * 4);
        var difImg = new Uint8ClampedArray(n * 4);

        var sum = 0;          /* oracle.c's: stops at the offending channel */
        var sumAll = 0;       /* gldiff.html's: every channel, always */
        var outliers = 0;
        var maxDiff = 0;      /* likewise truncated ... */
        var maxAll = 0;       /* ... and likewise not */

        for (var i = 0; i < n; i++) {
            var gi = i * 4, ri = i * 3;
            var broke = false;
            for (var c = 0; c < 3; c++) {
                var got = g[gi + c], want = r[ri + c];
                var d = got > want ? got - want : want - got;

                sumAll += d;
                if (d > maxAll) { maxAll = d; }
                if (!broke) {
                    sum += d;
                    if (d > maxDiff) { maxDiff = d; }
                    if (d > 8) {
                        outliers++;
                        broke = true;   /* oracle.c's `break` */
                    }
                }

                gpuImg[gi + c] = got;
                cpuImg[gi + c] = want;
                difImg[gi + c] = d * 8 > 255 ? 255 : d * 8;
            }
            gpuImg[gi + 3] = cpuImg[gi + 3] = difImg[gi + 3] = 255;
        }

        var mean = sum / (n * 3);
        var meanGldiff = sumAll / (n * 3);
        var pct = 100 * outliers / n;

        return {
            ok: mean < MEAN_BAR && pct < OUTLIER_PCT_BAR,
            width: w, height: h,
            mean: mean,
            meanGldiff: meanGldiff,
            /* True only when outliers truncated the sum; see the header. */
            meansDiverge: Math.abs(mean - meanGldiff) > 1e-12 || maxDiff !== maxAll,
            max: maxDiff,
            maxGldiff: maxAll,
            outliers: outliers,
            outlierPct: pct,
            meanBar: MEAN_BAR,
            outlierBar: OUTLIER_PCT_BAR,
            images: { gpu: gpuImg, cpu: cpuImg, diff: difImg }
        };
    }

    /* The one-line verdict, worded as an example prints it. */
    function line(d) {
        return 'DIFF ' + (d.ok ? 'OK' : 'FAIL') + ': ' + d.width + 'x' + d.height +
            ', mean err ' + d.mean.toFixed(4) + '/255, max ' + d.max +
            '/255, ' + d.outlierPct.toFixed(3) + '% pixels off by >8';
    }

    root.oracle = {
        parseRef: parseRef, diff: diff, line: line,
        MEAN_BAR: MEAN_BAR, OUTLIER_PCT_BAR: OUTLIER_PCT_BAR
    };
}(window.Hologram = window.Hologram || {}));
