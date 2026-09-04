/* The camera, flying or walking.
 *
 * FLYING has no capsule, no floor and no walls, and answers "what does this
 * panel look like from over there".
 *
 * WALKING is holo_walk_step, through the twin in core/collision.js, driven
 * by the twin of the fixed-step accumulator in core/timestep.js, at the
 * 120 Hz every walking example sets. It answers the different question of
 * whether a player can get somewhere and what they see when they do -- which
 * a flying camera will cheerfully lie about, because it can stand inside a
 * wall.
 *
 * The walk constants are the ones all three walking examples share: eyes at
 * 1.55 above the feet, 3.5 m/s, a 6 m/s jump. They are game constants rather
 * than engine ones and so are not in any dump; they are here as the defaults
 * those examples chose, and the eye height is adjustable because a game may
 * not choose the same.
 *
 * The basis is rebuilt through linalg.cameraMake in both modes, the same
 * construction camera.c uses, so the camera the shader receives from the
 * editor is built the way the camera the shader receives from a game is.
 */
(function (root) {
    'use strict';

    var L = root.linalg;
    var UP_HINT = L.v3(0, 1, 0);

    /* What every walking example uses. See the header. */
    var EYE = 1.55, WALK_SPEED = 3.5, JUMP = 6.0, WALK_HZ = 120;

    function create(doc) {
        var start = (doc && doc.camera) || {};
        var pos = L.fromArray(start.pos || [0, 1, 5]);
        var fwd = L.fromArray(start.forward || [0, 0, -1]);
        var fovDeg = start.fov_deg || 70;

        /* forward = (sin(yaw)cos(pitch), sin(pitch), -cos(yaw)cos(pitch)),
           so yaw 0 looks down -Z, which is where every example starts. */
        var yaw = Math.atan2(fwd.x, -fwd.z);
        var pitch = Math.asin(Math.max(-1, Math.min(1, fwd.y)));
        var home = { pos: pos, yaw: yaw, pitch: pitch };

        var LIMIT = Math.PI / 2 - 0.001;   /* never straight up: the roll flips */

        function forward() {
            var cp = Math.cos(pitch);
            return L.v3(Math.sin(yaw) * cp, Math.sin(pitch), -Math.cos(yaw) * cp);
        }

        function look(dx, dy) {
            yaw += dx;
            pitch -= dy;
            pitch = Math.max(-LIMIT, Math.min(LIMIT, pitch));
        }

        /* Movement is in the camera's frame but flattened, so looking at the
           floor and walking forward does not drive you into it. `up` is the
           world's, which is what a person means by "up" while flying. */
        function move(right, up, ahead, distance) {
            var f = forward();
            var flat = L.norm(L.v3(f.x, 0, f.z));
            var side = L.norm(L.cross(flat, UP_HINT));
            pos = L.add(pos, L.scale(side, right * distance));
            pos = L.add(pos, L.scale(flat, ahead * distance));
            pos = L.add(pos, L.scale(UP_HINT, up * distance));
        }

        function reset() {
            pos = home.pos;
            yaw = home.yaw;
            pitch = home.pitch;
        }

        /* Turn to face a point, and back off far enough to see something of
           the given size. Used to jump to whatever is selected in the list,
           because hunting for a rect you just added by flying around is the
           single most tedious thing an editor can make you do. */
        function frame(target, size) {
            var back = Math.max(size, 0.25) * 2.5 + 1;
            var f = L.norm(L.sub(target, pos));
            if (L.len(L.sub(target, pos)) < 1e-4) {
                f = L.v3(0, 0, -1);
            }
            pos = L.sub(target, L.scale(f, back));
            yaw = Math.atan2(f.x, -f.z);
            pitch = Math.asin(Math.max(-1, Math.min(1, f.y)));
        }

        /* ---- walking ------------------------------------------------- */

        var world = null;      /* set when a walk world has been loaded */
        var walker = null;
        var walking = false;
        var eye = EYE;

        function setWorld(walkDoc) {
            if (!walkDoc) { return; }
            world = root.collision.loadWorld(walkDoc.world);
            walker = root.collision.makeWalker(walkDoc.start);
            eye = Math.max(0.1, world.height - 0.15);   /* 1.7 - 0.15 = 1.55 */
            root.timestep.setHz(WALK_HZ);
        }

        function canWalk() { return world !== null; }

        function setWalking(on) {
            if (on && !world) { return false; }
            walking = !!on;
            if (walking) {
                /* Step in from wherever the flying camera left off, so
                   turning walking on does not teleport you. The walker owns
                   the feet; the eye follows from them. */
                walker.pos.x = pos.x;
                walker.pos.z = pos.z;
                walker.pos.y = world.floor_y;
                walker.vel.x = walker.vel.y = walker.vel.z = 0;
                root.timestep.reset();
            }
            return walking;
        }

        function isWalking() { return walking; }

        /* One frame of walking: hand the real elapsed time to the
           accumulator and run however many fixed steps come out, exactly as
           the examples' before_frame does. `held` is the key state. */
        function walkFrame(dt, held) {
            if (!walking || !world) { return; }
            root.timestep.advance(dt);
            var steps = root.timestep.steps();
            var stepDt = root.timestep.dt();
            for (var s = 0; s < steps; s++) {
                var fx = Math.sin(yaw), fz = -Math.cos(yaw);
                var wishF = (held.w ? 1 : 0) - (held.s ? 1 : 0);
                var wishR = (held.d ? 1 : 0) - (held.a ? 1 : 0);
                walker.vel.x = (fx * wishF - fz * wishR) * WALK_SPEED;
                walker.vel.z = (fz * wishF + fx * wishR) * WALK_SPEED;
                if (walker.grounded && held.jump) {
                    walker.vel.y = JUMP;
                }
                root.collision.step(walker, world, stepDt);
            }
            pos = L.v3(walker.pos.x, walker.pos.y + eye, walker.pos.z);
        }

        /* The camera as the block wants it: a basis, built the way
           camera.c builds one. */
        function basis(aspect) {
            return L.cameraMake(pos, L.add(pos, forward()), UP_HINT, fovDeg, aspect);
        }

        return {
            basis: basis,
            look: look,
            move: move,
            reset: reset,
            frame: frame,
            setWorld: setWorld,
            canWalk: canWalk,
            setWalking: setWalking,
            isWalking: isWalking,
            walkFrame: walkFrame,
            walker: function () { return walker; },
            eye: function () { return eye; },
            state: function () {
                return { pos: pos, yaw: yaw, pitch: pitch, fovDeg: fovDeg };
            },
            setFov: function (deg) { fovDeg = deg; }
        };
    }

    root.camera = { create: create };
}(window.Hologram = window.Hologram || {}));
