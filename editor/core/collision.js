/* source/collision.c, in JavaScript.
 *
 * A twin, and the only one here that no rendered image would catch drifting:
 * a packer bug shows up as a wrong picture, a walk bug shows up as a room
 * you can walk through the wall of, which looks fine in a screenshot. So it
 * is checked directly instead. Every example that owns a walker dumps
 * build/<name>_walk.json with the world AND a trace of 660 scripted steps --
 * what was commanded, and what came out. conformance() replays the commands
 * through this code and compares positions. See source/walk_json.h.
 *
 * Math.fround at every step, for the reason core/linalg.js gives: the engine
 * computes in float and rounding only at the end is a different answer.
 */
(function (root) {
    'use strict';

    var f = Math.fround;

    /* Does the walker's vertical span overlap this wall's? Feet exactly at a
       wall's top do not -- standing on the floor beside a low wall is not a
       collision with it. */
    function spansOverlap(pos, height, b) {
        return pos.y < b.max[1] && f(pos.y + height) > b.min[1];
    }

    /* Move one horizontal axis and clamp back out of any wall entered.
       axis 0 is x, 2 is z. Walls are expanded by the radius, so this is a
       point test and the clamp lands the capsule flush against the face. */
    function moveAxis(w, world, delta, axis) {
        if (axis === 0) {
            w.pos.x = f(w.pos.x + delta);
        } else {
            w.pos.z = f(w.pos.z + delta);
        }
        for (var i = 0; i < world.walls.length; i++) {
            var b = world.walls[i];
            if (!spansOverlap(w.pos, world.height, b)) {
                continue;
            }
            var minx = f(b.min[0] - world.radius);
            var maxx = f(b.max[0] + world.radius);
            var minz = f(b.min[2] - world.radius);
            var maxz = f(b.max[2] + world.radius);
            if (w.pos.x <= minx || w.pos.x >= maxx ||
                w.pos.z <= minz || w.pos.z >= maxz) {
                continue;
            }
            /* Inside: back out along the axis that just moved, on the side
               the movement came from. */
            if (axis === 0) {
                w.pos.x = delta > 0 ? minx : maxx;
            } else {
                w.pos.z = delta > 0 ? minz : maxz;
            }
        }
    }

    /* holo_walk_step. vel.x and vel.z are the caller's to set each step --
       walking is kinematic; vel.y belongs to gravity and jumps. */
    function step(w, world, dt) {
        moveAxis(w, world, f(w.vel.x * dt), 0);
        moveAxis(w, world, f(w.vel.z * dt), 2);

        w.vel.y = f(w.vel.y - f(world.gravity * dt));
        w.pos.y = f(w.pos.y + f(w.vel.y * dt));
        if (w.pos.y <= world.floor_y) {
            w.pos.y = world.floor_y;
            w.vel.y = 0;
            w.grounded = 1;
        } else {
            w.grounded = 0;
        }
    }

    function makeWalker(start) {
        return {
            pos: { x: f(start.pos[0]), y: f(start.pos[1]), z: f(start.pos[2]) },
            vel: { x: f(start.vel[0]), y: f(start.vel[1]), z: f(start.vel[2]) },
            grounded: start.grounded ? 1 : 0
        };
    }

    /* Every number out of the JSON has to be put back through fround before
       it is used or compared.
     *
       walk_json.c writes the shortest text that reads back as the same
       FLOAT; JSON.parse hands back a DOUBLE. Those are not the same number.
       "0.06666667" is the exact spelling of a particular binary32, and
       parsing it to binary64 lands about 1e-9 away from that value -- so
       comparing a frounded result against the raw parse disagrees in the
       last places on almost every row, which looks like a broken twin and
       is a broken comparison. */
    function loadWorld(w) {
        return {
            radius: f(w.radius),
            height: f(w.height),
            gravity: f(w.gravity),
            floor_y: f(w.floor_y),
            walls: (w.walls || []).map(function (b) {
                return { min: b.min.map(f), max: b.max.map(f) };
            })
        };
    }

    /* Replay a dumped trace and hold this code to the C that wrote it.
     *
     * Each row is [vx, vz, jump, px, py, pz, grounded]: set the horizontal
     * velocity, apply the jump if there is one, take one step, and land on
     * the same position. Replayed statefully -- vel.y carries between steps
     * -- so a gravity error accumulates and shows rather than being reset
     * away every row. */
    function conformance(doc, tol) {
        tol = tol === undefined ? 0 : tol;   /* bit-exact is the bar */
        var world = loadWorld(doc.world), trace = doc.trace;
        var w = makeWalker(doc.start);
        var dt = f(trace.dt);
        var worst = { d: 0, step: -1, want: null, got: null };
        var exact = 0, differing = 0, groundedMismatch = 0;

        for (var i = 0; i < trace.steps.length; i++) {
            var r = trace.steps[i];
            w.vel.x = f(r[0]);
            w.vel.z = f(r[1]);
            if (r[2] !== 0) { w.vel.y = f(r[2]); }
            step(w, world, dt);

            var wantX = f(r[3]), wantY = f(r[4]), wantZ = f(r[5]);
            var dx = Math.abs(w.pos.x - wantX);
            var dy = Math.abs(w.pos.y - wantY);
            var dz = Math.abs(w.pos.z - wantZ);
            var d = Math.max(dx, dy, dz);
            if (d === 0) { exact++; } else { differing++; }
            if ((w.grounded ? 1 : 0) !== r[6]) { groundedMismatch++; }
            if (d > worst.d) {
                worst = { d: d, step: i,
                          want: [wantX, wantY, wantZ],
                          got: [w.pos.x, w.pos.y, w.pos.z] };
            }
        }

        var n = trace.steps.length;
        return {
            ok: worst.d <= tol && groundedMismatch === 0,
            steps: n,
            exact: exact,
            differing: differing,
            groundedMismatch: groundedMismatch,
            worst: worst,
            tol: tol
        };
    }

    root.collision = {
        step: step, makeWalker: makeWalker, loadWorld: loadWorld,
        conformance: conformance
    };
}(window.Hologram = window.Hologram || {}));
