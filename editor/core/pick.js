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

    var NONE = 0, SPHERE = 1, RECT = 2, DISH = 3, FLOOR = 4;

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

    root.pick = {
        NONE: NONE, SPHERE: SPHERE, RECT: RECT, DISH: DISH, FLOOR: FLOOR,
        pick: pick, cameraRay: cameraRay, conformance: conformance,
        raySphere: raySphere, rayRect: rayRect, rayDish: rayDish,
        rayPlane: rayPlane
    };
}(window.Hologram = window.Hologram || {}));
