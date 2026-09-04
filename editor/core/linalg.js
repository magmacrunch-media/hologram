/* source/linalg.c, in JavaScript.
 *
 * Every arithmetic step is put through Math.fround. That looks like
 * ceremony and is not: hologram computes in float, deliberately -- see the
 * note in linalg.h about the CPU oracle computing in the precision the GPU
 * will -- while JavaScript computes in double. Rounding only at the end,
 * when the number is stored into a Float32Array, gives a different answer
 * from rounding at every step, and the difference lands in the packed
 * uniform block the conformance check compares against C's.
 *
 * It will still not be bit-exact everywhere: Math.exp is not expf, and a
 * compiler is free to contract a multiply-add. Getting within an ulp is
 * enough for the check, which is looking for a field in the wrong slot --
 * an error of order the field's own magnitude, not its last bit.
 */
(function (root) {
    'use strict';

    var f = Math.fround;

    function v3(x, y, z) {
        return { x: f(x), y: f(y), z: f(z) };
    }

    function add(a, b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
    function sub(a, b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
    function scale(a, s) { s = f(s); return v3(a.x * s, a.y * s, a.z * s); }

    /* C evaluates this as ((x*x) + (y*y)) + (z*z), rounding to float at each
       step; the parentheses and fround calls here say exactly that. */
    function dot(a, b) {
        return f(f(f(a.x * b.x) + f(a.y * b.y)) + f(a.z * b.z));
    }

    function cross(a, b) {
        return v3(f(a.y * b.z) - f(a.z * b.y),
                  f(a.z * b.x) - f(a.x * b.z),
                  f(a.x * b.y) - f(a.y * b.x));
    }

    function len(a) { return f(Math.sqrt(dot(a, a))); }

    /* hv3_norm returns `a` itself below 1e-8, rather than a division by
       something near zero. The reciprocal is taken first, as in the C. */
    function norm(a) {
        var l = len(a);
        return l > 1e-8 ? scale(a, f(1 / l)) : a;
    }

    function fromArray(t) { return v3(t[0], t[1], t[2]); }

    /* geometry.c's holo_rect_basis. The normal is derived from the solve
       vectors, not the edges -- the comment there explains that the shaders
       derive it the same way, and this must too or the panels the editor
       shows are not the panels the tracer intersects. */
    function rectBasis(edgeU, edgeV) {
        var uu = dot(edgeU, edgeU), vv = dot(edgeV, edgeV);
        var uv = dot(edgeU, edgeV);
        var invDet = f(1 / f(f(uu * vv) - f(uv * uv)));
        var solveU = scale(sub(scale(edgeU, vv), scale(edgeV, uv)), invDet);
        var solveV = scale(sub(scale(edgeV, uu), scale(edgeU, uv)), invDet);
        return { normal: norm(cross(solveU, solveV)), solveU: solveU, solveV: solveV };
    }

    /* camera.c's holo_camera_make. up_hint only breaks the roll ambiguity
       and need not be perpendicular to the view. */
    function cameraMake(pos, target, upHint, fovDeg, aspect) {
        var forward = norm(sub(target, pos));
        var right = norm(cross(forward, upHint));
        return {
            pos: pos,
            forward: forward,
            right: right,
            up: cross(right, forward),
            tan_half_fov: f(Math.tan(f(f(f(fovDeg * 0.5) * 3.14159265358979) / 180))),
            aspect: f(aspect)
        };
    }

    root.linalg = {
        f: f, v3: v3, add: add, sub: sub, scale: scale, dot: dot,
        cross: cross, len: len, norm: norm, fromArray: fromArray,
        rectBasis: rectBasis, cameraMake: cameraMake
    };
}(window.Hologram = window.Hologram || {}));
