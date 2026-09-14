/* source/geometry.c's intersections, and cpu_trace.c's nearest_hit, in
 * JavaScript -- enough to answer "what did I just click on".
 *
 * The tracer returns a colour, not an identity, so clicking a primitive in
 * the rendered image means intersecting the scene here. That is a copy of
 * engine code, and the rule this editor has followed throughout is that a
 * copy is checked against the thing it copies. Every dumped example now
 * carries build/<name>_pick.json: a grid of rays through its camera and,
 * for each, the primitive the ENGINE found nearest. conformance() casts the
 * same grid through the code below and compares.
 *
 * The traversal order matters and is not stylistic. cpu_trace.c walks
 * spheres, then rects, then dishes, then the floor, keeping a hit only when
 * `t < best` strictly -- so on an exact tie the earlier one wins. Two
 * surfaces that touch are decided by that, and walking them in another order
 * disagrees along every shared edge in the room.
 *
 * Math.fround throughout, for the reason core/linalg.js gives.
 *
 * WHAT PRECISION MEANS HERE, which is different from the packer. A picker
 * owes the right object, not the right distance. Where two surfaces meet at
 * a grazing angle the two implementations can legitimately land on opposite
 * sides of an edge, exactly as the oracle diff tolerates razor edges in an
 * image -- so the bar is a small share of the grid, not zero, and the
 * measured figure is in the editor's README.
 */
(function (root) {
    'use strict';

    var L = root.linalg, f = Math.fround;

    var T_MIN = 1e-3;

    var NONE = 0, SPHERE = 1, RECT = 2, DISH = 3, FLOOR = 4, FRESNEL = 5;
    var FRESNEL_WINDOW = 3;   /* HOLO_FRESNEL_WINDOW */

    /* holo_ray_sphere. */
    function raySphere(ro, rd, center, radius) {
        var oc = L.sub(ro, center);
        var b = L.dot(oc, rd);
        var c = f(L.dot(oc, oc) - f(radius * radius));
        var disc = f(f(b * b) - c);
        if (disc <= 0) { return -1; }
        var sq = f(Math.sqrt(disc));
        var t = f(-b - sq);
        if (t <= T_MIN) { t = f(-b + sq); }
        return t > T_MIN ? t : -1;
    }

    /* holo_ray_plane. */
    function rayPlane(ro, rd, point, normal) {
        var denom = L.dot(rd, normal);
        if (Math.abs(denom) < 1e-6) { return -1; }
        var t = f(L.dot(L.sub(point, ro), normal) / denom);
        return t > T_MIN ? t : -1;
    }

    /* holo_ray_rect, through the same basis holo_rect_basis builds -- which
       linalg.js already carries, because gpu_scene needs it too. */
    function rayRect(ro, rd, corner, edgeU, edgeV) {
        var basis = L.rectBasis(edgeU, edgeV);
        var t = rayPlane(ro, rd, corner, basis.normal);
        if (t < 0) { return -1; }
        var p = L.add(ro, L.scale(rd, t));
        var rel = L.sub(p, corner);
        var u = L.dot(rel, basis.solveU);
        var v = L.dot(rel, basis.solveV);
        return (u >= 0 && u <= 1 && v >= 0 && v <= 1) ? t : -1;
    }

    /* holo_basis: an orthonormal frame around a unit axis, deterministic so
       the CPU and GPU build the same one -- and so does this. */
    function basis(axis) {
        var helper = Math.abs(axis.x) > 0.9 ? L.v3(0, 1, 0) : L.v3(1, 0, 0);
        var u = L.norm(L.cross(helper, axis));
        return { u: u, v: L.cross(axis, u) };
    }

    /* holo_ray_dish, line for line.
     *
       Two things here are easy to leave out and were: the cap is clipped in
       z as well as in radius, because an ellipsoid has a far half and a
       hyperboloid a second sheet that are not part of the dish; and when the
       near root lands on the clipped part the FAR root may still be the
       visible bowl, which is what happens every time you look into a concave
       mirror. Getting either wrong reports a dish in front of the floor
       across a good part of the frame. */
    function rayDish(ro, rd, apex, axis, curvR, conicK, rim) {
        var fr = basis(axis);
        var rel = L.sub(ro, apex);
        var o = L.v3(L.dot(rel, fr.u), L.dot(rel, fr.v), L.dot(rel, axis));
        var d = L.v3(L.dot(rd, fr.u), L.dot(rd, fr.v), L.dot(rd, axis));

        var p = f(1 + conicK);
        var A = f(f(f(d.x * d.x) + f(d.y * d.y)) + f(p * f(d.z * d.z)));
        var B = f(f(f(f(o.x * d.x) + f(o.y * d.y)) + f(f(p * o.z) * d.z)) -
                  f(curvR * d.z));
        var C = f(f(f(f(o.x * o.x) + f(o.y * o.y)) + f(f(p * o.z) * o.z)) -
                  f(f(2 * curvR) * o.z));

        var rr = f(rim * rim);
        var root = f(1 - f(f(p * rr) / f(curvR * curvR)));
        var zMax = f(rr / f(curvR * f(1 + f(Math.sqrt(root > 0 ? root : 0)))));

        var t1, t2;
        if (Math.abs(A) < 1e-8) {
            /* An axis-parallel ray on a paraboloid: one crossing. */
            if (Math.abs(B) < 1e-12) { return -1; }
            t1 = f(-C / f(2 * B));
            t2 = -1;
        } else {
            var disc = f(f(B * B) - f(A * C));
            if (disc < 0) { return -1; }
            var sq = f(Math.sqrt(disc));
            t1 = f(f(-B - sq) / A);
            t2 = f(f(-B + sq) / A);
        }

        for (var pass = 0; pass < 2; pass++) {
            var t = pass === 0 ? t1 : t2;
            if (t <= T_MIN) { continue; }
            var z = f(o.z + f(t * d.z));
            var x = f(o.x + f(t * d.x));
            var y = f(o.y + f(t * d.y));
            if (z < 0 || z > zMax || f(f(x * x) + f(y * y)) > rr) { continue; }
            return t;
        }
        return -1;
    }

    /* holo_fresnel_tilt: one atan2, exact. */
    function fresnelTilt(r, focal, n) {
        var h = f(Math.sqrt(f(f(r * r) + f(focal * focal))));
        return f(Math.atan2(r, f(f(n * h) - focal)));
    }

    /* holo_ray_fresnel's pieces, each competing for the nearest t through
       `best` -- an object, since JavaScript has no inout parameter. */
    function fresnelCylinder(P0, P1, P2, o, d, R, zLo, zHi, best) {
        if (P2 < 1e-12) { return; }
        var disc = f(f(P1 * P1) - f(P2 * f(P0 - f(R * R))));
        if (disc < 0) { return; }
        var sq = f(Math.sqrt(disc));
        for (var side = 0; side < 2; side++) {
            var t = f(f(side === 0 ? -P1 - sq : -P1 + sq) / P2);
            var z = f(o.z + f(t * d.z));
            if (t > T_MIN && t < best.t && z >= zLo && z <= zHi) { best.t = t; }
        }
    }

    function fresnelFacet(P0, P1, P2, o, d, rk, rOut, s, cs, best) {
        var q0 = f(f(rk * s) - f(o.z * cs));
        var qd = f(-d.z * cs);
        var A = f(f(f(s * s) * P2) - f(qd * qd));
        var B = f(f(f(s * s) * P1) - f(q0 * qd));
        var C = f(f(f(s * s) * P0) - f(q0 * q0));
        var t1, t2;
        if (Math.abs(A) < 1e-10) {
            if (Math.abs(B) < 1e-12) { return; }
            t1 = f(-C / f(2 * B));
            t2 = -1;
        } else {
            var disc = f(f(B * B) - f(A * C));
            if (disc < 0) { return; }
            var sq = f(Math.sqrt(disc));
            t1 = f(f(-B - sq) / A);
            t2 = f(f(-B + sq) / A);
        }
        for (var side = 0; side < 2; side++) {
            var t = side === 0 ? t1 : t2;
            var x = f(o.x + f(t * d.x)), y = f(o.y + f(t * d.y));
            var z = f(o.z + f(t * d.z));
            var rho = f(Math.sqrt(f(f(x * x) + f(y * y))));
            var q = f(f(rk * s) - f(z * cs));
            if (t > T_MIN && t < best.t && q > -1e-6 &&
                rho >= rk && rho <= rOut) {
                best.t = t;
            }
        }
    }

    /* holo_ray_fresnel, line for line: the slab-and-cylinder reject, the
       back face, the walls, then a fixed window of rings seeded from the
       radius where the ray enters the slab, each ring's tilt computed from
       its index. Identity only, so the normal is not carried. */
    function rayFresnel(ro, rd, center, axis, focal, n, r0, pitch, rim, thick) {
        var fr = basis(axis);
        var rel = L.sub(ro, center);
        var o = L.v3(L.dot(rel, fr.u), L.dot(rel, fr.v), L.dot(rel, axis));
        var d = L.v3(L.dot(rd, fr.u), L.dot(rd, fr.v), L.dot(rd, axis));
        var P2 = f(f(d.x * d.x) + f(d.y * d.y));
        var P1 = f(f(o.x * d.x) + f(o.y * d.y));
        var P0 = f(f(o.x * o.x) + f(o.y * o.y));
        var rr = f(rim * rim);

        var lo = 0, hi = 1e30;
        if (Math.abs(d.z) < 1e-8) {
            if (o.z < -thick || o.z > 0) { return -1; }
        } else {
            var ta = f(f(-thick - o.z) / d.z), tb = f(-o.z / d.z);
            if (ta > tb) { var sw = ta; ta = tb; tb = sw; }
            if (ta > lo) { lo = ta; }
            if (tb < hi) { hi = tb; }
        }
        if (P2 < 1e-12) {
            if (P0 > rr) { return -1; }
        } else {
            var disc = f(f(P1 * P1) - f(P2 * f(P0 - rr)));
            if (disc < 0) { return -1; }
            var sq = f(Math.sqrt(disc));
            var ca = f(f(-P1 - sq) / P2), cb = f(f(-P1 + sq) / P2);
            if (ca > lo) { lo = ca; }
            if (cb < hi) { hi = cb; }
        }
        if (hi < lo || hi <= T_MIN) { return -1; }

        var rings = Math.ceil(f(f(rim - r0) / pitch) - 1e-4);
        if (rings < 1) { return -1; }

        var best = { t: 1e30 };
        if (Math.abs(d.z) > 1e-8) {
            var t = f(f(-thick - o.z) / d.z);
            var q = f(f(P0 + f(f(2 * P1) * t)) + f(P2 * f(t * t)));
            if (t > T_MIN && q <= rr && q >= f(r0 * r0)) { best.t = t; }
        }
        var rkLast = f(r0 + f((rings - 1) * pitch));
        var top = f(-f(rim - rkLast) * f(Math.tan(fresnelTilt(rkLast, focal, n))));
        fresnelCylinder(P0, P1, P2, o, d, rim, -thick, top, best);
        if (r0 > 0) { fresnelCylinder(P0, P1, P2, o, d, r0, -thick, 0, best); }

        var rhoSeed = f(Math.sqrt(f(f(P0 + f(f(2 * P1) * lo)) + f(P2 * f(lo * lo)))));
        var kSeed = Math.floor(f(f(rhoSeed - r0) / pitch));
        if (kSeed < 0) { kSeed = 0; }
        if (kSeed > rings - 1) { kSeed = rings - 1; }
        for (var i = -FRESNEL_WINDOW; i <= FRESNEL_WINDOW; i++) {
            var k = kSeed + i;
            if (k >= 0 && k < rings) {
                var rk = f(r0 + f(k * pitch));
                var rOut = f(rk + pitch) < rim ? f(rk + pitch) : rim;
                var a = fresnelTilt(rk, focal, n);
                var s = f(Math.sin(a)), cs = f(Math.cos(a));
                fresnelFacet(P0, P1, P2, o, d, rk, rOut, s, cs, best);
                if (k < rings - 1) {
                    var depth = f(f(pitch * s) / cs);
                    fresnelCylinder(P0, P1, P2, o, d, f(rk + pitch), -depth, 0, best);
                }
            }
        }
        return best.t >= 1e29 ? -1 : best.t;
    }

    /* cpu_trace.c's nearest_hit, reporting identity instead of a surface. */
    function pick(doc, ro, rd) {
        var best = 1e30, kind = NONE, index = -1;
        var i, t;

        var spheres = doc.spheres || [];
        for (i = 0; i < spheres.length; i++) {
            t = raySphere(ro, rd, L.fromArray(spheres[i].center),
                          f(spheres[i].radius));
            if (t > 0 && t < best) { best = t; kind = SPHERE; index = i; }
        }
        var rects = doc.rects || [];
        for (i = 0; i < rects.length; i++) {
            t = rayRect(ro, rd, L.fromArray(rects[i].corner),
                        L.fromArray(rects[i].edge_u),
                        L.fromArray(rects[i].edge_v));
            if (t > 0 && t < best) { best = t; kind = RECT; index = i; }
        }
        var dishes = doc.dishes || [];
        for (i = 0; i < dishes.length; i++) {
            /* The axis is taken as unit, as the C takes it. */
            t = rayDish(ro, rd, L.fromArray(dishes[i].apex),
                        L.fromArray(dishes[i].axis), f(dishes[i].curv_r),
                        f(dishes[i].conic_k), f(dishes[i].rim));
            if (t > 0 && t < best) { best = t; kind = DISH; index = i; }
        }
        var fresnels = doc.fresnels || [];
        for (i = 0; i < fresnels.length; i++) {
            var fl = fresnels[i];
            t = rayFresnel(ro, rd, L.fromArray(fl.center), L.fromArray(fl.axis),
                           f(fl.focal), f(fl.ior), f(fl.r0 || 0), f(fl.pitch),
                           f(fl.rim), f(fl.thick));
            if (t > 0 && t < best) { best = t; kind = FRESNEL; index = i; }
        }
        var floor = doc.floor || {};
        if (floor.has_floor) {
            t = rayPlane(ro, rd, L.v3(0, floor.floor_y || 0, 0), L.v3(0, 1, 0));
            if (t > 0 && t < best) { best = t; kind = FLOOR; index = -1; }
        }
        return { kind: kind, index: index, t: best === 1e30 ? -1 : best };
    }

    /* holo_camera_ray. */
    function cameraRay(cam, u, v) {
        var x = f(f(f(u * 2 - 1) * cam.tan_half_fov) * cam.aspect);
        var y = f(f(1 - v * 2) * cam.tan_half_fov);
        var dir = L.add(cam.forward,
                        L.add(L.scale(cam.right, x), L.scale(cam.up, y)));
        return { origin: cam.pos, dir: L.norm(dir) };
    }

    /* Replay a dumped grid. `doc` is the scene as dumped; `pickDoc` is
       build/<name>_pick.json, which carries its own camera so the wrong one
       cannot be used by accident. */
    function conformance(doc, pickDoc) {
        var cam = {
            pos: L.fromArray(pickDoc.camera.pos),
            forward: L.fromArray(pickDoc.camera.forward),
            right: L.fromArray(pickDoc.camera.right),
            up: L.fromArray(pickDoc.camera.up),
            tan_half_fov: f(pickDoc.camera.tan_half_fov),
            aspect: f(pickDoc.camera.aspect)
        };
        var cols = pickDoc.grid.cols, rows = pickDoc.grid.rows;
        var hits = pickDoc.hits;
        var total = 0, agree = 0;
        var byKind = {};
        var samples = [];

        for (var y = 0; y < rows; y++) {
            for (var x = 0; x < cols; x++) {
                var n = (y * cols + x) * 2;
                var wantKind = hits[n], wantIndex = hits[n + 1];
                var r = cameraRay(cam, f((x + 0.5) / cols), f((y + 0.5) / rows));
                var got = pick(doc, r.origin, r.dir);
                total++;
                if (got.kind === wantKind && got.index === wantIndex) {
                    agree++;
                } else {
                    var key = wantKind + '->' + got.kind;
                    byKind[key] = (byKind[key] || 0) + 1;
                    if (samples.length < 6) {
                        samples.push({ x: x, y: y,
                                       want: [wantKind, wantIndex],
                                       got: [got.kind, got.index] });
                    }
                }
            }
        }

        var pct = 100 * (total - agree) / total;
        return {
            /* The same spirit as the oracle's outlier allowance: an edge is
               allowed to fall the other way, a whole primitive is not. */
            ok: pct <= 0.5,
            total: total, agree: agree, differ: total - agree,
            pct: pct, byKind: byKind, samples: samples
        };
    }


    /* ---- where a thing lands on the frame ------------------------------ *
     *
     * The inverse of cameraRay above. Not a twin of engine code -- the
     * engine only ever casts rays, it never projects -- but it has to agree
     * with cameraRay or the outline sits where the object is not, so it is
     * written as that function read backwards rather than derived afresh.
     *
     *     x = (u * 2 - 1) * tan_half_fov * aspect,  x = dot(d, right) / z
     *     y = (1 - v * 2) * tan_half_fov,           y = dot(d, up) / z
     */
    function project(cam, p) {
        var d = L.sub(p, cam.pos);
        var z = L.dot(d, cam.forward);
        if (z <= 1e-6) {
            return null;          /* behind the eye: no answer, not a big one */
        }
        return {
            u: (L.dot(d, cam.right) / (z * cam.tan_half_fov * cam.aspect) + 1) * 0.5,
            v: (1 - L.dot(d, cam.up) / (z * cam.tan_half_fov)) * 0.5
        };
    }

    /* Points that bound the primitive: a sphere's AABB, a rect's four
       corners, a dish's apex and the ring of its rim. The dish's rim sits at
       the cap's sag, which is the same z_max rayDish clips to -- a ring
       drawn at the apex plane would sit inside a deep bowl. */
    function hullOf(doc, sel) {
        var pts = [], i, o;
        if (!sel || !doc) {
            return pts;
        }
        if (sel.list === 'spheres' && doc.spheres && doc.spheres[sel.index]) {
            o = doc.spheres[sel.index];
            var c = L.fromArray(o.center), r = o.radius || 0;
            for (i = 0; i < 8; i++) {
                pts.push(L.v3(c.x + ((i & 1) ? r : -r),
                              c.y + ((i & 2) ? r : -r),
                              c.z + ((i & 4) ? r : -r)));
            }
        } else if (sel.list === 'rects' && doc.rects && doc.rects[sel.index]) {
            o = doc.rects[sel.index];
            var k = L.fromArray(o.corner);
            var eu = L.fromArray(o.edge_u), ev = L.fromArray(o.edge_v);
            pts.push(k, L.add(k, eu), L.add(k, ev), L.add(k, L.add(eu, ev)));
        } else if (sel.list === 'dishes' && doc.dishes && doc.dishes[sel.index]) {
            o = doc.dishes[sel.index];
            var apex = L.fromArray(o.apex), axis = L.norm(L.fromArray(o.axis));
            var rim = o.rim || 0, R = o.curv_r || 1, K = o.conic_k || 0;
            var rr = rim * rim;
            var disc = 1 - (1 + K) * rr / (R * R);
            var sag = rr / (R * (1 + Math.sqrt(disc > 0 ? disc : 0)));
            var b = basis(axis);
            var lip = L.add(apex, L.scale(axis, sag));
            pts.push(apex);
            for (i = 0; i < 12; i++) {
                var a = i * Math.PI / 6;
                pts.push(L.add(lip, L.add(L.scale(b.u, rim * Math.cos(a)),
                                          L.scale(b.v, rim * Math.sin(a)))));
            }
        } else if (sel.list === 'fresnels' && doc.fresnels &&
                   doc.fresnels[sel.index]) {
            /* A short cylinder: the rim ring at the peak plane and again at
               the back face, which is what bounds a slab of rings. */
            o = doc.fresnels[sel.index];
            var fc = L.fromArray(o.center), fax = L.norm(L.fromArray(o.axis));
            var frim = o.rim || 0, fb = basis(fax);
            var back = L.add(fc, L.scale(fax, -(o.thick || 0)));
            pts.push(fc, back);
            for (i = 0; i < 12; i++) {
                var fa = i * Math.PI / 6;
                var ring = L.add(L.scale(fb.u, frim * Math.cos(fa)),
                                 L.scale(fb.v, frim * Math.sin(fa)));
                pts.push(L.add(fc, ring), L.add(back, ring));
            }
        }
        return pts;
    }

    /* The selection's bounding rectangle on the frame, as fractions of width
       and height, or null for "do not draw one".

       Null when ANY bounding point is behind the eye. The projection has no
       finite answer there, and a box built from the points that happen to be
       in front is not a smaller truth -- stand inside a room and select the
       wall behind you and it would draw a neat little rectangle off to one
       side of a wall that fills the screen. A missing outline reads as
       "cannot say"; a confident wrong one reads as a bug in the picker. */
    function screenBox(doc, sel, cam) {
        var pts = hullOf(doc, sel);
        if (!pts.length) {
            return null;
        }
        var u0 = 2, v0 = 2, u1 = -1, v1 = -1;
        for (var i = 0; i < pts.length; i++) {
            var s = project(cam, pts[i]);
            if (!s) {
                return null;
            }
            if (s.u < u0) { u0 = s.u; }
            if (s.u > u1) { u1 = s.u; }
            if (s.v < v0) { v0 = s.v; }
            if (s.v > v1) { v1 = s.v; }
        }
        /* Entirely off to one side: nothing to point at. */
        if (u1 <= 0 || u0 >= 1 || v1 <= 0 || v0 >= 1) {
            return null;
        }
        return {
            u0: Math.max(u0, 0), v0: Math.max(v0, 0),
            u1: Math.min(u1, 1), v1: Math.min(v1, 1)
        };
    }

    root.pick = {
        NONE: NONE, SPHERE: SPHERE, RECT: RECT, DISH: DISH, FLOOR: FLOOR,
        FRESNEL: FRESNEL,
        pick: pick, cameraRay: cameraRay, conformance: conformance,
        raySphere: raySphere, rayRect: rayRect, rayDish: rayDish,
        rayFresnel: rayFresnel, fresnelTilt: fresnelTilt,
        rayPlane: rayPlane, project: project, screenBox: screenBox
    };
}(window.Hologram = window.Hologram || {}));
