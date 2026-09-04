/* A scene, back out as the C that built it.
 *
 * hologram builds scenes in code and this does not change that: the editor's
 * output is a HoloScene designated-initializer block to paste into a game,
 * the shape examples/m7_room/main.c is written in. There is no loader and
 * there is not going to be one -- scene_json.h has the reasons -- so this is
 * the whole path from "tuned it until it looked right" to "the game does
 * that now".
 *
 * Two rules make the output readable:
 *
 *   Zero fields are omitted. A designated initializer zero-fills what it
 *   does not name, so naming them adds nothing and hides the fields that
 *   matter. This is why the emitted rects are three lines and not fifteen.
 *
 *   Floats keep the spelling the JSON gave them. scene_json.c already found
 *   the shortest text that reads back bit-exact, so 0.85 stays "0.85f" and
 *   is not re-derived into 0.850000024f here.
 */
(function (root) {
    'use strict';

    var S = root.scene;

    /* The shortest text that reads back as the same float, plus the suffix
       that makes C read it as one.
     *
     * This is scene_json.c's spelling loop, in JavaScript, and it has to be
     * here rather than trusted from the JSON. Numbers that came from a
     * dumped scene are already short, but numbers the editor computed are
     * not: the camera's position is float32 arithmetic seen through a
     * double, so 1.55f prints as 1.5499999523162842, and `normalize` writes
     * 0.7071067690849304. Emitting those is not wrong, but it is unreadable,
     * and a scene pasted into a game should look like something a person
     * would have typed.
     *
     * The round-trip is checked through Math.fround, which is the same
     * question strtof answers in the C: does this text name this float.
     *
     * The suffix matters beyond tidiness. Without it every literal is a
     * double, the initialiser promotes, and the value the game compiles is
     * not bit-for-bit the value the editor rendered. */
    /* The number alone, at that shortest spelling. Shared with core/save.js,
       which writes JSON and so wants the digits without C's suffix.
     *
       Spelled the way C's %g spells it, in two details that do not change
       the value and do change the text. printf pads an exponent to at least
       two digits (e-08, not e-8), and prints negative zero with its sign,
       while String(-0) in JavaScript is "0". Matching both is what lets a
       saved scene be diffed against the dump it came from and show only the
       edits. */
    function num(n) {
        if (!isFinite(n)) { return '0'; }
        var v = Math.fround(n);
        var s = null;
        for (var p = 1; p <= 9 && s === null; p++) {
            var t = v.toPrecision(p);
            if (Math.fround(parseFloat(t)) === v) {
                s = String(parseFloat(t));
            }
        }
        if (s === null) { s = String(v); }
        if (s === '0' && Object.is(v, -0)) { s = '-0'; }
        return s.replace(/e([+-])(\d)$/, 'e$10$2');
    }

    function fl(n) {
        if (!isFinite(n)) { return '0.0f'; }
        var s = num(n);
        if (s.indexOf('e') >= 0 || s.indexOf('E') >= 0) { return s + 'f'; }
        if (s.indexOf('.') < 0) { s += '.0'; }
        return s + 'f';
    }

    function v3(a) {
        a = a || [0, 0, 0];
        return '{ ' + fl(a[0]) + ', ' + fl(a[1]) + ', ' + fl(a[2]) + ' }';
    }

    function isZeroV3(a) {
        return !a || (!a[0] && !a[1] && !a[2]);
    }

    /* One ".key = value" per non-default field, wrapped to a sensible width.
       `parts` is built by each primitive below; the wrapping is shared so
       every block in the output breaks the same way. */
    function block(parts, indent, width) {
        var lines = [], line = '';
        for (var i = 0; i < parts.length; i++) {
            var piece = parts[i] + (i + 1 < parts.length ? ',' : '');
            if (line && (line + ' ' + piece).length > width) {
                lines.push(line);
                line = piece;
            } else {
                line = line ? line + ' ' + piece : piece;
            }
        }
        if (line) { lines.push(line); }
        return lines.map(function (l, i) {
            return (i === 0 ? '' : indent) + l;
        }).join('\n');
    }

    function filterName(v) {
        for (var i = 0; i < root.schema.FILTER_OPTIONS.length; i++) {
            if (root.schema.FILTER_OPTIONS[i].value === v) {
                return root.schema.FILTER_OPTIONS[i].c;
            }
        }
        return String(v);
    }

    function sphereParts(s) {
        var p = [];
        p.push('.center = ' + v3(s.center));
        p.push('.radius = ' + fl(s.radius));
        if (!isZeroV3(s.albedo)) { p.push('.albedo = ' + v3(s.albedo)); }
        if (s.mirror) { p.push('.mirror = ' + fl(s.mirror)); }
        if (s.transmit) { p.push('.transmit = ' + fl(s.transmit)); }
        if (s.ior) { p.push('.ior = ' + fl(s.ior)); }
        if (s.disperse) { p.push('.disperse = ' + fl(s.disperse)); }
        return p;
    }

    function rectParts(r) {
        var p = [];
        p.push('.corner = ' + v3(r.corner));
        p.push('.edge_u = ' + v3(r.edge_u));
        p.push('.edge_v = ' + v3(r.edge_v));
        if (!isZeroV3(r.albedo)) { p.push('.albedo = ' + v3(r.albedo)); }
        if (r.mirror) { p.push('.mirror = ' + fl(r.mirror)); }
        if (r.transmit) { p.push('.transmit = ' + fl(r.transmit)); }
        if (r.ior) { p.push('.ior = ' + fl(r.ior)); }
        if (r.disperse) { p.push('.disperse = ' + fl(r.disperse)); }
        if (r.filter) { p.push('.filter = ' + filterName(r.filter)); }
        if (r.filter_angle) { p.push('.filter_angle = ' + fl(r.filter_angle)); }
        if (r.retard) { p.push('.retard = ' + fl(r.retard)); }
        if (r.grating_period) {
            p.push('.grating_period = ' + fl(r.grating_period));
            if (r.grating_angle) {
                p.push('.grating_angle = ' + fl(r.grating_angle));
            }
            var w = r.order_w || [0, 0, 0, 0];
            p.push('.order_w = { ' + w.map(fl).join(', ') + ' }');
        }
        return p;
    }

    function dishParts(d) {
        var p = [];
        p.push('.apex = ' + v3(d.apex));
        p.push('.axis = ' + v3(d.axis));
        p.push('.curv_r = ' + fl(d.curv_r));
        p.push('.conic_k = ' + fl(d.conic_k));
        p.push('.rim = ' + fl(d.rim));
        if (!isZeroV3(d.albedo)) { p.push('.albedo = ' + v3(d.albedo)); }
        if (d.mirror) { p.push('.mirror = ' + fl(d.mirror)); }
        return p;
    }

    function list(items, parts, indent) {
        return items.map(function (o) {
            return indent + '{ ' + block(parts(o), indent + '  ', 66) + ' },';
        }).join('\n');
    }

    /* The whole scene as one assignment. `name` is the variable to assign
       to; the caller usually wants "scene", matching the examples. */
    function toC(doc, name) {
        name = name || 'scene';
        var floor = doc.floor || {}, sky = doc.sky || {};
        var spheres = doc.spheres || [], rects = doc.rects || [];
        var dishes = doc.dishes || [];
        var out = [];

        out.push(name + ' = (HoloScene){');

        if (spheres.length) {
            out.push('    .spheres = {');
            out.push(list(spheres, sphereParts, '        '));
            out.push('    },');
            out.push('    .sphere_count = ' + spheres.length + ',');
        }
        if (rects.length) {
            out.push('    .rects = {');
            out.push(list(rects, rectParts, '        '));
            out.push('    },');
            out.push('    .rect_count = ' + rects.length + ',');
        }
        if (dishes.length) {
            out.push('    .dishes = {');
            out.push(list(dishes, dishParts, '        '));
            out.push('    },');
            out.push('    .dish_count = ' + dishes.length + ',');
        }

        if (floor.has_floor) {
            out.push('    .has_floor = 1,');
            out.push('    .floor_y = ' + fl(floor.floor_y || 0) + ',');
            if (!isZeroV3(floor.floor_a)) {
                out.push('    .floor_a = ' + v3(floor.floor_a) + ',');
            }
            if (!isZeroV3(floor.floor_b)) {
                out.push('    .floor_b = ' + v3(floor.floor_b) + ',');
            }
            if (floor.floor_mirror) {
                out.push('    .floor_mirror = ' + fl(floor.floor_mirror) + ',');
            }
        }

        if (!isZeroV3(sky.sun_dir)) {
            out.push('    .sun_dir = ' + v3(sky.sun_dir) + ',');
        }
        if (!isZeroV3(sky.horizon)) {
            out.push('    .horizon = ' + v3(sky.horizon) + ',');
        }
        if (!isZeroV3(sky.zenith)) {
            out.push('    .zenith  = ' + v3(sky.zenith) + ',');
        }
        if (sky.sun_disk_intensity) {
            out.push('    .sun_disk_cos = ' + fl(sky.sun_disk_cos) + ',');
            out.push('    .sun_disk_intensity = ' +
                     fl(sky.sun_disk_intensity) + ',');
        }

        out.push('};');
        return out.join('\n') + '\n';
    }

    /* The camera as the two lines that build one. The editor flies a basis
       around, but a game writes holo_camera_make(pos, target, up, fov,
       aspect) -- so the target is handed back as pos + forward, which is
       what the basis came from. */
    function cameraToC(cam, aspect) {
        var s = cam.state();
        var f = root.linalg.v3(Math.sin(s.yaw) * Math.cos(s.pitch),
                               Math.sin(s.pitch),
                               -Math.cos(s.yaw) * Math.cos(s.pitch));
        var t = root.linalg.add(s.pos, f);
        return 'cam = holo_camera_make(hv3(' + fl(s.pos.x) + ', ' +
            fl(s.pos.y) + ', ' + fl(s.pos.z) + '),\n' +
            '                       hv3(' + fl(t.x) + ', ' + fl(t.y) + ', ' +
            fl(t.z) + '),\n' +
            '                       hv3(0, 1, 0), ' + fl(s.fovDeg) +
            ', ' + (aspect ? fl(aspect) : 'aspect') + ');\n';
    }

    root.emit = { toC: toC, cameraToC: cameraToC, fl: fl, num: num };
}(window.Hologram = window.Hologram || {}));
