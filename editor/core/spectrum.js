/* source/spectrum.c's two exported halves, in JavaScript.
 *
 * Only holo_lambda and holo_spectral_weight are here, because only those
 * two reach the uniform block -- gpu_scene.c writes the twelve
 * (lambda, weight) pairs into spectral_lw, and the shader is forbidden from
 * re-deriving them (gpu_scene.h: "CPU and GPU folding the same floats is
 * part of what the oracle diff certifies"). holo_albedo_at and holo_ior_at
 * live inside the tracers and never leave them, so they are not needed to
 * pack a scene and are not duplicated here.
 */
(function (root) {
    'use strict';

    var L = root.linalg, f = L.f;

    var WAVELENGTHS = 12;

    /* 0.42f + (0.68f - 0.42f) * i / 11, with the subtraction folded the way
       a C compiler folds it: to a float, once. */
    var SPAN = f(f(0.68) - f(0.42));

    function lambda(i) {
        return f(f(0.42) + f(f(SPAN * i) / f(WAVELENGTHS - 1)));
    }

    /* A Wyman-Sloan-Shirley lobe: a Gaussian with different widths either
       side of centre. nm, as published. */
    function lobe(nm, center, sl, sr, amp) {
        var s = nm < center ? sl : sr;
        var t = f(f(nm - center) / s);
        return f(amp * f(Math.exp(f(-0.5 * f(t * t)))));
    }

    function cmfX(nm) {
        return f(f(lobe(nm, 599.8, 37.9, 31.0, 1.056) +
                   lobe(nm, 442.0, 16.0, 26.7, 0.362)) +
                 lobe(nm, 501.1, 20.4, 26.2, -0.065));
    }

    function cmfY(nm) {
        return f(lobe(nm, 568.8, 46.9, 40.5, 0.821) +
                 lobe(nm, 530.9, 16.3, 31.1, 0.286));
    }

    function cmfZ(nm) {
        return f(lobe(nm, 437.0, 11.8, 36.0, 1.217) +
                 lobe(nm, 459.0, 26.0, 13.8, 0.681));
    }

    /* XYZ -> linear sRGB (D65), then normalised per channel so a flat
       spectrum lands on exact white. The whole ladder is summed on every
       call, exactly as the C does, because the normaliser needs it. */
    function weight(i) {
        var sum = L.v3(0, 0, 0), mine = L.v3(0, 0, 0);
        for (var k = 0; k < WAVELENGTHS; k++) {
            var nm = f(lambda(k) * 1000);
            var x = cmfX(nm), y = cmfY(nm), z = cmfZ(nm);
            var rgb = L.v3(
                f(f(f(3.2406 * x) - f(1.5372 * y)) - f(0.4986 * z)),
                f(f(f(-0.9689 * x) + f(1.8758 * y)) + f(0.0415 * z)),
                f(f(f(0.0557 * x) - f(0.204 * y)) + f(1.057 * z)));
            sum = L.add(sum, rgb);
            if (k === i) {
                mine = rgb;
            }
        }
        return L.v3(f(mine.x / sum.x), f(mine.y / sum.y), f(mine.z / sum.z));
    }

    root.spectrum = { WAVELENGTHS: WAVELENGTHS, lambda: lambda, weight: weight };
}(window.Hologram = window.Hologram || {}));
