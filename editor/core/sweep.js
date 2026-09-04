/* Measuring the render, rather than restating the theory beside it.
 *
 * A probe is a rectangle of the frame read as one number, the way a
 * photodiode on a bench reads the beam that lands on it. A sweep steps one
 * scene parameter across a range, renders at each step, and reports what the
 * probe saw. Malus's law comes out of turning a polarizer and watching the
 * light behind it, not out of a cosine written here.
 *
 * That distinction is the whole design. The editor already keeps one twin of
 * engine code (core/scene.js, and it is checked against C on every load);
 * a second copy of the optics -- Fresnel, Malus, Cauchy, the grating
 * equation -- would be four more, checked by nothing, and would answer
 * questions about itself rather than about the tracer. A curve measured off
 * the rendered frame is a statement about what hologram actually does, and
 * it is falsifiable against the closed-form values tests/test_polar.c and
 * tests/test_linalg.c already pin.
 *
 * THE ONE THING THAT MUST BE RIGHT: the frame is sRGB-encoded. oracle.c's
 * encode_u8 applies the transfer curve on the way out, so the bytes are not
 * proportional to intensity, and averaging them measures nothing physical --
 * a cos^2 read straight off the bytes comes out visibly wrong. Every value
 * here is decoded back to linear first.
 */
(function (root) {
    'use strict';

    /* encode_u8's transfer, inverted. 256 entries, built once: the sweep
       decodes a few million samples and the pow is not free. */
    var LINEAR = (function () {
        var t = new Float64Array(256);
        for (var i = 0; i < 256; i++) {
            var v = i / 255;
            t[i] = v <= 0.04045 ? v / 12.92
                                : Math.pow((v + 0.055) / 1.055, 2.4);
        }
        return t;
    }());

    /* Rec. 709 luma, which is the weighting that turns linear RGB into the
       one number a detector would report. */
    function luminance(r, g, b) {
        return 0.2126 * LINEAR[r] + 0.7152 * LINEAR[g] + 0.0722 * LINEAR[b];
    }

    /* Mean linear luminance over `rect` of an RGBA frame, rows top-down.
       The rect is clipped to the frame; an empty one reads 0. */
    function measure(frame, rect) {
        var x0 = Math.max(0, Math.round(rect.x));
        var y0 = Math.max(0, Math.round(rect.y));
        var x1 = Math.min(frame.width, Math.round(rect.x + rect.w));
        var y1 = Math.min(frame.height, Math.round(rect.y + rect.h));
        if (x1 <= x0 || y1 <= y0) {
            return { mean: 0, min: 0, max: 0, samples: 0 };
        }
        var px = frame.rgba;
        var sum = 0, min = Infinity, max = -Infinity, n = 0;
        for (var y = y0; y < y1; y++) {
            var row = y * frame.width * 4;
            for (var x = x0; x < x1; x++) {
                var i = row + x * 4;
                var l = luminance(px[i], px[i + 1], px[i + 2]);
                sum += l;
                if (l < min) { min = l; }
                if (l > max) { max = l; }
                n++;
            }
        }
        return { mean: sum / n, min: min, max: max, samples: n };
    }

    var DEG = 180 / Math.PI;

    /* Which fields of a primitive are worth sweeping: the scalars. Vectors
       and colours are excluded -- a sweep needs one number with an order to
       it, and "sweep the albedo" is three questions, not one. Angles are
       offered in degrees because that is what a person turning a polarizer
       is thinking in, and converted back on the way into the scene. */
    function targets(fields, obj) {
        var out = [];
        fields.forEach(function (f) {
            if (f.kind === 'vec3' || f.kind === 'vec4' ||
                f.kind === 'color' || f.kind === 'bool' || f.kind === 'enum') {
                return;
            }
            var isAngle = f.kind === 'angle';
            out.push({
                key: f.key,
                label: (f.label || f.key) + (isAngle ? ' (deg)' : ''),
                unit: isAngle ? 'deg' : (f.unit || ''),
                /* Sensible defaults for the range, per kind. */
                from: isAngle ? 0 : (f.kind === 'unit' ? 0 : (f.min !== undefined ? f.min : 0)),
                to: isAngle ? 180 : (f.kind === 'unit' ? 1 : (f.max !== undefined ? f.max : 1)),
                get: function () {
                    var v = obj[f.key] || 0;
                    return isAngle ? v * DEG : v;
                },
                set: function (v) {
                    obj[f.key] = isAngle ? v / DEG : v;
                },
                inert: f.inert ? f.inert(obj) : null
            });
        });
        return out;
    }

    /* Summary of a finished sweep. `extinction` is max/min -- the number a
       polarizer or a filter is actually judged by, and infinite when the
       minimum reads a true zero. */
    function summarise(points) {
        if (!points.length) { return null; }
        var lo = points[0], hi = points[0];
        points.forEach(function (p) {
            if (p.mean < lo.mean) { lo = p; }
            if (p.mean > hi.mean) { hi = p; }
        });
        return {
            min: lo.mean, minAt: lo.v,
            max: hi.mean, maxAt: hi.v,
            extinction: lo.mean > 0 ? hi.mean / lo.mean : Infinity,
            /* Normalised, so a curve can be compared with a textbook one
               without caring what the source brightness was. */
            normalised: points.map(function (p) {
                return { v: p.v, y: hi.mean > 0 ? p.mean / hi.mean : 0 };
            })
        };
    }

    function toCsv(points, label) {
        var rows = [(label || 'parameter') + ',mean_linear,min,max'];
        points.forEach(function (p) {
            rows.push([p.v, p.mean, p.min, p.max].join(','));
        });
        return rows.join('\n') + '\n';
    }

    root.sweep = {
        measure: measure, targets: targets, summarise: summarise,
        toCsv: toCsv, luminance: luminance, LINEAR: LINEAR
    };
}(window.Hologram = window.Hologram || {}));
