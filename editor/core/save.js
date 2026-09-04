/* A scene, written back out in the format source/scene_json.c writes.
 *
 * The point of matching that format exactly is that a saved file is a
 * drop-in: the editor opens its own output the way it opens a dump, and
 * anything else that learns to read hologram/scene/1 reads both. The key
 * order, the indentation and the float spelling all follow the C, so a save
 * of an untouched scene differs from the file it came from only in the
 * provenance block below -- which is worth having a diff prove rather than
 * a comment claim.
 *
 * WHAT A SAVE IS NOT
 *
 * It is not a write back over build/<name>_scene.json. That file is what
 * --dump produced, and build/<name>_params.bin and _ref.bin beside it are a
 * render OF that scene: the packer banner and the oracle panel are both
 * checked against them. Overwrite the scene with an edited one and both
 * would go on comparing confidently against references belonging to a
 * different room. So a save goes wherever you point it, the dump stays the
 * dump, and a document opened from a file is told plainly that it has no
 * references to be checked against.
 */
(function (root) {
    'use strict';

    var num = function (n) { return root.emit.num(n); };

    function v3(a) {
        a = a || [0, 0, 0];
        return '[' + num(a[0]) + ', ' + num(a[1]) + ', ' + num(a[2]) + ']';
    }

    function line(indent, key, value, tail) {
        return indent + '"' + key + '": ' + value + tail;
    }

    function sphere(s, tail) {
        return '    {\n' +
            line('      ', 'center', v3(s.center), ',\n') +
            line('      ', 'radius', num(s.radius || 0), ',\n') +
            line('      ', 'albedo', v3(s.albedo), ',\n') +
            line('      ', 'mirror', num(s.mirror || 0), ',\n') +
            line('      ', 'transmit', num(s.transmit || 0), ',\n') +
            line('      ', 'ior', num(s.ior || 0), ',\n') +
            line('      ', 'disperse', num(s.disperse || 0), '\n') +
            '    }' + tail;
    }

    function rect(r, tail) {
        var w = r.order_w || [0, 0, 0, 0];
        return '    {\n' +
            line('      ', 'corner', v3(r.corner), ',\n') +
            line('      ', 'edge_u', v3(r.edge_u), ',\n') +
            line('      ', 'edge_v', v3(r.edge_v), ',\n') +
            line('      ', 'albedo', v3(r.albedo), ',\n') +
            line('      ', 'mirror', num(r.mirror || 0), ',\n') +
            line('      ', 'transmit', num(r.transmit || 0), ',\n') +
            line('      ', 'ior', num(r.ior || 0), ',\n') +
            line('      ', 'disperse', num(r.disperse || 0), ',\n') +
            line('      ', 'filter', String(r.filter || 0), ',\n') +
            line('      ', 'filter_angle', num(r.filter_angle || 0), ',\n') +
            line('      ', 'retard', num(r.retard || 0), ',\n') +
            line('      ', 'grating_period', num(r.grating_period || 0), ',\n') +
            line('      ', 'grating_angle', num(r.grating_angle || 0), ',\n') +
            line('      ', 'order_w',
                 '[' + w.map(num).join(', ') + ']', '\n') +
            '    }' + tail;
    }

    function dish(d, tail) {
        return '    {\n' +
            line('      ', 'apex', v3(d.apex), ',\n') +
            line('      ', 'axis', v3(d.axis), ',\n') +
            line('      ', 'curv_r', num(d.curv_r || 0), ',\n') +
            line('      ', 'conic_k', num(d.conic_k || 0), ',\n') +
            line('      ', 'rim', num(d.rim || 0), ',\n') +
            line('      ', 'albedo', v3(d.albedo), ',\n') +
            line('      ', 'mirror', num(d.mirror || 0), '\n') +
            '    }' + tail;
    }

    function list(items, one) {
        return items.map(function (o, i) {
            return one(o, i + 1 < items.length ? ',\n' : '\n');
        }).join('');
    }

    /* `origin` names the dump this document started from, so a reader (and a
       person) can tell an edited scene from one the engine wrote. */
    function toJson(doc, origin) {
        var caps = doc.caps || root.caps.DEFAULTS;
        var cam = doc.camera || {};
        var floor = doc.floor || {}, sky = doc.sky || {};
        var out = '';

        out += '{\n';
        out += line('  ', 'format', '"hologram/scene/1"', ',\n');
        out += line('  ', 'spectral', doc.spectral ? '1' : '0', ',\n');

        /* Not written by scene_json.c: this says the file came from the
           editor and which dump it started as, which is how the packer and
           oracle panels know their references do not belong to it. */
        out += '  "editor": {\n';
        out += line('    ', 'edited', '1', ',\n');
        out += line('    ', 'origin', JSON.stringify(origin || null), '\n');
        out += '  },\n';

        out += '  "caps": {\n';
        out += line('    ', 'spheres', String(caps.spheres), ',\n');
        out += line('    ', 'rects', String(caps.rects), ',\n');
        out += line('    ', 'dishes', String(caps.dishes), ',\n');
        out += line('    ', 'gpu_gratings', String(caps.gpu_gratings), ',\n');
        out += line('    ', 'bounce', String(caps.bounce), ',\n');
        out += line('    ', 'rays', String(caps.rays), '\n');
        out += '  },\n';

        out += '  "camera": {\n';
        out += line('    ', 'pos', v3(cam.pos), ',\n');
        out += line('    ', 'forward', v3(cam.forward), ',\n');
        out += line('    ', 'right', v3(cam.right), ',\n');
        out += line('    ', 'up', v3(cam.up), ',\n');
        out += line('    ', 'tan_half_fov', num(cam.tan_half_fov || 0), ',\n');
        out += line('    ', 'aspect', num(cam.aspect || 0), ',\n');
        out += line('    ', 'fov_deg', num(cam.fov_deg || 0), '\n');
        out += '  },\n';

        out += '  "spheres": [\n' + list(doc.spheres || [], sphere) + '  ],\n';
        out += '  "rects": [\n' + list(doc.rects || [], rect) + '  ],\n';
        out += '  "dishes": [\n' + list(doc.dishes || [], dish) + '  ],\n';

        out += '  "floor": {\n';
        out += line('    ', 'has_floor', floor.has_floor ? '1' : '0', ',\n');
        out += line('    ', 'floor_y', num(floor.floor_y || 0), ',\n');
        out += line('    ', 'floor_a', v3(floor.floor_a), ',\n');
        out += line('    ', 'floor_b', v3(floor.floor_b), ',\n');
        out += line('    ', 'floor_mirror', num(floor.floor_mirror || 0), '\n');
        out += '  },\n';

        out += '  "sky": {\n';
        out += line('    ', 'sun_dir', v3(sky.sun_dir), ',\n');
        out += line('    ', 'horizon', v3(sky.horizon), ',\n');
        out += line('    ', 'zenith', v3(sky.zenith), ',\n');
        out += line('    ', 'sun_disk_cos', num(sky.sun_disk_cos || 0), ',\n');
        out += line('    ', 'sun_disk_intensity',
                    num(sky.sun_disk_intensity || 0), '\n');
        out += '  }\n';
        out += '}\n';
        return out;
    }

    /* The walk world, in walk_json.c's format.
     *
       Written WITHOUT a trace, and that omission is the honest part. A trace
       is a record of the C stepping this world; the editor cannot produce
       one, and inventing rows from its own copy of the walk step would be a
       twin marking its own homework. A reader gets the world and is told
       plainly there is nothing here to check the arithmetic against -- the
       synthetic selftest in build/walk_selftest.json still does that, and it
       does not depend on any particular world. */
    function walkToJson(world, start, origin) {
        var out = '';
        out += '{\n';
        out += line('  ', 'format', '"hologram/walk/1"', ',\n');
        out += '  "editor": {\n';
        out += line('    ', 'edited', '1', ',\n');
        out += line('    ', 'origin', JSON.stringify(origin || null), ',\n');
        out += line('    ', 'note',
                    '"no trace: written by the editor, not by a walk"', '\n');
        out += '  },\n';
        out += '  "world": {\n';
        out += line('    ', 'radius', num(world.radius || 0), ',\n');
        out += line('    ', 'height', num(world.height || 0), ',\n');
        out += line('    ', 'gravity', num(world.gravity || 0), ',\n');
        out += line('    ', 'floor_y', num(world.floor_y || 0), ',\n');
        out += line('    ', 'max_walls', String(root.schema.MAX_WALLS), ',\n');
        out += '    "walls": [\n';
        (world.walls || []).forEach(function (b, i, all) {
            out += '      { "min": ' + v3(b.min) + ', "max": ' + v3(b.max) +
                   ' }' + (i + 1 < all.length ? ',\n' : '\n');
        });
        out += '    ]\n  },\n';
        var s = start || { pos: [0, 0, 0], vel: [0, 0, 0], grounded: 1 };
        out += '  "start": { "pos": ' + v3(s.pos) + ', "vel": ' + v3(s.vel) +
               ', "grounded": ' + (s.grounded ? 1 : 0) + ' }\n';
        out += '}\n';
        return out;
    }

    /* Where the camera is now, in the shape the format wants. A save records
       where you were standing, which is most of why you would reopen it. */
    function cameraFrom(cam, aspect) {
        var b = cam.basis(aspect);
        var s = cam.state();
        return {
            pos: [b.pos.x, b.pos.y, b.pos.z],
            forward: [b.forward.x, b.forward.y, b.forward.z],
            right: [b.right.x, b.right.y, b.right.z],
            up: [b.up.x, b.up.y, b.up.z],
            tan_half_fov: b.tan_half_fov,
            aspect: b.aspect,
            fov_deg: s.fovDeg
        };
    }

    /* Drafts are per scene name, so two tabs on two rooms do not fight. */
    function draftKey(name) { return 'hologram.editor.draft.' + name; }

    function saveDraft(name, doc) {
        try {
            window.localStorage.setItem(draftKey(name), JSON.stringify({
                at: Date.now(),
                doc: doc
            }));
            return true;
        } catch (e) {
            /* Quota, private mode, storage disabled -- a draft is a
               convenience and its loss is not worth an error. */
            return false;
        }
    }

    function readDraft(name) {
        try {
            var raw = window.localStorage.getItem(draftKey(name));
            return raw ? JSON.parse(raw) : null;
        } catch (e) {
            return null;
        }
    }

    function clearDraft(name) {
        try {
            window.localStorage.removeItem(draftKey(name));
        } catch (e) { /* nothing to do about it */ }
    }

    root.save = {
        toJson: toJson, walkToJson: walkToJson, cameraFrom: cameraFrom,
        saveDraft: saveDraft, readDraft: readDraft, clearDraft: clearDraft,
        draftKey: draftKey
    };
}(window.Hologram = window.Hologram || {}));
