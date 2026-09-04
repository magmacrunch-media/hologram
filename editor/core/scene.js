/* source/gpu_scene.c, in JavaScript: a scene packed into the uniform block
 * the shader receives.
 *
 * THIS IS A TWIN. cpu_trace.c, trace.hlsl, trace.glsl and trace.metal are
 * already four statements of the same tracer held together by the oracle
 * diff; this is a fifth statement of the block's LAYOUT, and it has the same
 * obligation. gpu_scene.h says it plainly: the struct and the shader "must
 * change together -- the oracle diff is what catches them drifting apart."
 *
 * Two things hold this copy honest, and neither is discipline:
 *
 *   1. LAYOUT below carries each field's slot number as the shader spells
 *      it AND is checked against a running total. Add a field in the middle
 *      of HoloGpuScene and update only one of the two, and loading the
 *      editor throws by name instead of rendering something plausible.
 *
 *   2. conformance() packs a scene the C already packed -- every example's
 *      --dump writes build/<name>_params.bin, which IS the C-packed block --
 *      and compares float by float. A field written to the wrong slot is off
 *      by the size of the field, not by an ulp, so it cannot hide.
 */
(function (root) {
    'use strict';

    var L = root.linalg, S = root.spectrum, f = L.f;

    var MAX_SPHERES = 8, MAX_RECTS = 24, MAX_DISHES = 4;
    var FILTER_NONE = 0, POLARIZER = 1, WAVEPLATE = 2;
    var GRATING_ORDERS = 4;

    /* Name, slot count, and the base slot the shader's #define says. Order
       is HoloGpuScene's declaration order; see shaders/trace.glsl:31-80. */
    var LAYOUT = [
        ['display', 1, 0],
        ['cam_pos', 1, 1],
        ['cam_fwd', 1, 2],
        ['cam_right', 1, 3],
        ['cam_up', 1, 4],
        ['sun_dir', 1, 5],
        ['horizon', 1, 6],
        ['zenith', 1, 7],
        ['floor_a', 1, 8],
        ['floor_b', 1, 9],
        ['sph_center_radius', MAX_SPHERES, 10],
        ['sph_albedo_mirror', MAX_SPHERES, 18],
        ['sph_glass', MAX_SPHERES, 26],
        ['rect_corner_mirror', MAX_RECTS, 34],
        ['rect_solve_u', MAX_RECTS, 58],
        ['rect_solve_v', MAX_RECTS, 82],
        ['rect_albedo', MAX_RECTS, 106],
        ['rect_glass', MAX_RECTS, 130],
        ['rect_filter', MAX_RECTS, 154],
        ['dish_apex_r', MAX_DISHES, 178],
        ['dish_axis_k', MAX_DISHES, 182],
        ['dish_albedo_mirror', MAX_DISHES, 186],
        ['dish_rim_count', MAX_DISHES, 190],
        ['spectral_lw', S.WAVELENGTHS, 194],
        ['grat0_groove_idx', 1, 206],
        ['grat0_period_w', 1, 207],
        ['grat1_groove_idx', 1, 208],
        ['grat1_period_w', 1, 209],
        ['grat_w2', 1, 210]
    ];

    /* Slot base per field, and the check that the declared bases and the
       running total tell the same story. */
    var SLOT = {};
    var TOTAL_SLOTS = (function () {
        var at = 0;
        for (var i = 0; i < LAYOUT.length; i++) {
            var name = LAYOUT[i][0], count = LAYOUT[i][1], declared = LAYOUT[i][2];
            if (at !== declared) {
                throw new Error('scene.js layout: ' + name + ' is at slot ' + at +
                                ' by count but declared at ' + declared +
                                ' -- HoloGpuScene and this table have drifted');
            }
            SLOT[name] = at;
            at += count;
        }
        return at;
    }());

    if (TOTAL_SLOTS !== 211) {
        throw new Error('scene.js layout: ' + TOTAL_SLOTS +
                        ' slots, but shaders/trace.glsl declares vec4 params[211]');
    }

    var TOTAL_FLOATS = TOTAL_SLOTS * 4;

    /* Which field a float index belongs to -- so a conformance failure can
       say "rect_solve_v[7].y", which names the bug, instead of "float 342". */
    function fieldAt(floatIndex) {
        var slot = Math.floor(floatIndex / 4);
        var lane = 'xyzw'[floatIndex % 4];
        for (var i = LAYOUT.length - 1; i >= 0; i--) {
            if (slot >= LAYOUT[i][2]) {
                var name = LAYOUT[i][0];
                var n = slot - LAYOUT[i][2];
                return LAYOUT[i][1] > 1 ? name + '[' + n + '].' + lane
                                        : name + '.' + lane;
            }
        }
        return 'slot ' + slot + '.' + lane;
    }

    function put3(out, base, v) {
        out[base] = v.x;
        out[base + 1] = v.y;
        out[base + 2] = v.z;
    }

    function v(a) { return L.fromArray(a || [0, 0, 0]); }

    /* The axis of a filter, or the groove direction of a grating: an angle
       measured off edge_u, turned into a vector in the panel's plane. Both
       are the same construction in gpu_scene.c; the grating's is normalised
       afterwards and the filter's is not. */
    function inPlane(edgeU, edgeV, angle) {
        return L.add(L.scale(L.norm(edgeU), f(Math.cos(angle))),
                     L.scale(L.norm(edgeV), f(Math.sin(angle))));
    }

    /* holo_gpu_scene_fill. `cam` is a basis, not a pos/target pair -- what
       reaches the shader is the basis, and rebuilding one from a target
       would not land on the same floats. Slot 0 is left alone: display.c
       fills the resolution and time every frame, and so does the view. */
    function pack(doc, cam, spectral) {
        var out = new Float32Array(TOTAL_FLOATS);
        var spheres = doc.spheres || [], rects = doc.rects || [];
        var dishes = doc.dishes || [];
        var floor = doc.floor || {}, sky = doc.sky || {};
        var i;

        put3(out, SLOT.cam_pos * 4, cam.pos);
        out[SLOT.cam_pos * 4 + 3] = cam.tan_half_fov;
        put3(out, SLOT.cam_fwd * 4, cam.forward);
        out[SLOT.cam_fwd * 4 + 3] = spectral ? 1 : 0;
        put3(out, SLOT.cam_right * 4, cam.right);
        put3(out, SLOT.cam_up * 4, cam.up);

        out[SLOT.cam_right * 4 + 3] = spheres.length;
        out[SLOT.horizon * 4 + 3] = rects.length;
        out[SLOT.cam_up * 4 + 3] = floor.has_floor ? 1 : 0;
        out[SLOT.sun_dir * 4 + 3] = floor.floor_y || 0;
        out[SLOT.zenith * 4 + 3] = floor.floor_mirror || 0;

        put3(out, SLOT.sun_dir * 4, v(sky.sun_dir));
        put3(out, SLOT.horizon * 4, v(sky.horizon));
        put3(out, SLOT.zenith * 4, v(sky.zenith));
        put3(out, SLOT.floor_a * 4, v(floor.floor_a));
        out[SLOT.floor_a * 4 + 3] = sky.sun_disk_cos || 0;
        put3(out, SLOT.floor_b * 4, v(floor.floor_b));
        out[SLOT.floor_b * 4 + 3] = sky.sun_disk_intensity || 0;

        for (i = 0; i < spheres.length && i < MAX_SPHERES; i++) {
            var s = spheres[i];
            var b = (SLOT.sph_center_radius + i) * 4;
            put3(out, b, v(s.center));
            out[b + 3] = s.radius || 0;
            b = (SLOT.sph_albedo_mirror + i) * 4;
            put3(out, b, v(s.albedo));
            out[b + 3] = s.mirror || 0;
            b = (SLOT.sph_glass + i) * 4;
            out[b] = s.transmit || 0;
            out[b + 1] = s.ior || 0;
            out[b + 2] = s.disperse || 0;
        }

        for (i = 0; i < rects.length && i < MAX_RECTS; i++) {
            var r = rects[i];
            var edgeU = v(r.edge_u), edgeV = v(r.edge_v);
            var cm = (SLOT.rect_corner_mirror + i) * 4;
            put3(out, cm, v(r.corner));
            out[cm + 3] = r.mirror || 0;

            /* Computed here once, as in the C, rather than per ray in the
               shader. holo_rect_basis is the same call the CPU tracer makes,
               which is what keeps the oracle and the GPU intersecting the
               same rectangle. */
            var basis = L.rectBasis(edgeU, edgeV);
            put3(out, (SLOT.rect_solve_u + i) * 4, basis.solveU);
            put3(out, (SLOT.rect_solve_v + i) * 4, basis.solveV);

            var al = (SLOT.rect_albedo + i) * 4;
            put3(out, al, v(r.albedo));
            /* Filters and gratings handle the light completely, so their
               albedo and mirror are zeroed in the GPU block -- gpu_scene.c
               explains that this survives a shader compiler mishandling the
               branch's early exit, which fxc has done. The CPU keeps the
               real albedo; its control flow is not in question. */
            if ((r.filter || FILTER_NONE) !== FILTER_NONE ||
                (r.grating_period || 0) > 0) {
                out[al] = 0;
                out[al + 1] = 0;
                out[al + 2] = 0;
                out[cm + 3] = 0;
            }

            var g = (SLOT.rect_glass + i) * 4;
            out[g] = r.transmit || 0;
            out[g + 1] = r.ior || 0;
            out[g + 2] = r.disperse || 0;
            out[g + 3] = r.retard || 0;

            var fi = (SLOT.rect_filter + i) * 4;
            out[fi] = r.filter || FILTER_NONE;
            var axis = inPlane(edgeU, edgeV, r.filter_angle || 0);
            out[fi + 1] = axis.x;
            out[fi + 2] = axis.y;
            out[fi + 3] = axis.z;
        }

        /* The first two grating rects land in scalar slots; gpu_scene.h has
           the reason, and caps.js has the consequence of a third. */
        out[SLOT.grat0_groove_idx * 4 + 3] = -1;
        out[SLOT.grat1_groove_idx * 4 + 3] = -1;
        var slot = 0;
        for (i = 0; i < rects.length && i < MAX_RECTS && slot < 2; i++) {
            if (!((rects[i].grating_period || 0) > 0)) {
                continue;
            }
            var rr = rects[i];
            var groove = L.norm(inPlane(v(rr.edge_u), v(rr.edge_v),
                                        rr.grating_angle || 0));
            var gi = (slot === 0 ? SLOT.grat0_groove_idx : SLOT.grat1_groove_idx) * 4;
            var pw = (slot === 0 ? SLOT.grat0_period_w : SLOT.grat1_period_w) * 4;
            var w = rr.order_w || [0, 0, 0, 0];
            put3(out, gi, groove);
            out[gi + 3] = i;
            out[pw] = rr.grating_period;
            out[pw + 1] = w[0];
            out[pw + 2] = w[1];
            out[pw + 3] = w[2];
            out[SLOT.grat_w2 * 4 + slot] = w[3];
            slot++;
        }

        for (i = 0; i < dishes.length && i < MAX_DISHES; i++) {
            var d = dishes[i];
            var db = (SLOT.dish_apex_r + i) * 4;
            put3(out, db, v(d.apex));
            out[db + 3] = d.curv_r || 0;
            db = (SLOT.dish_axis_k + i) * 4;
            put3(out, db, v(d.axis));
            out[db + 3] = d.conic_k || 0;
            db = (SLOT.dish_albedo_mirror + i) * 4;
            put3(out, db, v(d.albedo));
            out[db + 3] = d.mirror || 0;
            out[(SLOT.dish_rim_count + i) * 4] = d.rim || 0;
        }
        /* Written unconditionally, after the loop, exactly as the C does --
           including when there are no dishes at all. */
        out[SLOT.dish_rim_count * 4 + 1] = dishes.length;

        for (i = 0; i < S.WAVELENGTHS; i++) {
            var wt = S.weight(i);
            var sb = (SLOT.spectral_lw + i) * 4;
            out[sb] = S.lambda(i);
            out[sb + 1] = wt.x;
            out[sb + 2] = wt.y;
            out[sb + 3] = wt.z;
        }

        return out;
    }

    /* The bar: |a - b| <= ATOL + RTOL * |a|.
     *
     * Both terms are needed, and the reason is worth writing down because
     * two more obvious rulers both fail here.
     *
     * A pure RELATIVE tolerance fails on values that are legitimately near
     * zero. spectral_lw[2].z -- a CIE blue weight out at the red end -- is
     * -0.000747, and C and JavaScript disagree about it in the eighth
     * decimal place. That is 3.7e-5 relatively, which looks like a bug and
     * is not.
     *
     * A pure ULP distance fails the same way, harder. Every filter axis in
     * m6_polarization is built from cos(pi/2): cosf gives -4.371e-8 and
     * Math.cos gives -7.321e-8. Both are zero to any meaning the renderer
     * has, but they are six million representable floats apart, because
     * near zero the floats are packed unimaginably densely.
     *
     * So: an absolute floor that says "below a millionth, nobody cares",
     * plus a relative term that keeps the check sharp on the large values
     * where the interesting fields live. Measured headroom across all eight
     * dumped examples is about 8x -- the worst real disagreement anywhere is
     * 5.96e-8 -- while a field written to the wrong slot lands off by its
     * own magnitude, 0.1 to 10, which is five to seven orders of magnitude
     * above this bar. The check gives up almost nothing.
     */
    var ATOL = 1e-6, RTOL = 1e-6;

    function agree(a, b) {
        return Math.abs(a - b) <= ATOL + RTOL * Math.abs(a);
    }

    /* Pack a scene C already packed, and compare. `golden` is the
     * Float32Array view of build/<name>_params.bin.
     *
     * Copied fields come back bit-identical -- all of the geometry does.
     * Only the computed ones can differ at all: the CIE weights (Math.exp
     * is not expf) and the filter and groove axes (Math.cos is not cosf).
     *
     * Slot 0 is skipped: display.c overwrites it with the live resolution
     * every frame, so whatever was dumped there describes that run's window.
     */
    function conformance(golden, packed) {
        var worst = { index: -1, abs: 0, rel: 0, golden: 0, packed: 0 };
        var differing = 0, compared = 0, failures = 0;
        var byField = {};

        var n = Math.min(golden.length, packed.length);
        for (var i = 4; i < n; i++) {
            var a = golden[i], b = packed[i];
            compared++;
            if (a === b) {
                continue;
            }
            differing++;

            var abs = Math.abs(a - b);
            var ok = agree(a, b);
            if (!ok) {
                failures++;
            }

            var name = fieldAt(i).replace(/\[\d+\]/, '[]').replace(/\.[xyzw]$/, '');
            if (!byField[name]) {
                byField[name] = { count: 0, abs: 0, failed: 0 };
            }
            byField[name].count++;
            byField[name].abs = Math.max(byField[name].abs, abs);
            byField[name].failed += ok ? 0 : 1;

            if (abs > worst.abs) {
                worst = {
                    index: i, abs: abs, golden: a, packed: b,
                    rel: abs / Math.max(Math.abs(a), 1e-30)
                };
            }
        }

        return {
            ok: golden.length === packed.length && failures === 0,
            sizeMatch: golden.length === packed.length,
            goldenFloats: golden.length,
            packedFloats: packed.length,
            compared: compared,
            differing: differing,
            identical: compared - differing,
            failures: failures,
            worst: worst,
            worstField: worst.index >= 0 ? fieldAt(worst.index) : null,
            byField: byField,
            atol: ATOL, rtol: RTOL
        };
    }

    root.scene = {
        MAX_SPHERES: MAX_SPHERES, MAX_RECTS: MAX_RECTS, MAX_DISHES: MAX_DISHES,
        FILTER_NONE: FILTER_NONE, POLARIZER: POLARIZER, WAVEPLATE: WAVEPLATE,
        GRATING_ORDERS: GRATING_ORDERS,
        LAYOUT: LAYOUT, SLOT: SLOT,
        TOTAL_SLOTS: TOTAL_SLOTS, TOTAL_FLOATS: TOTAL_FLOATS,
        fieldAt: fieldAt, pack: pack, conformance: conformance, agree: agree
    };
}(window.Hologram = window.Hologram || {}));
