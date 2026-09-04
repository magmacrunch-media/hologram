/* A free-flying camera, for looking at a scene that is being built.
 *
 * Deliberately NOT holo_walk_step: this flies, with no capsule, no floor and
 * no walls, because the question it answers is "what does this panel look
 * like from over there", not "can the player get there". Walking the scene
 * the way the game will needs collision.c and timestep.c ported as twins,
 * and a twin that nothing checks is a liability -- so that waits until it is
 * worth the check.
 *
 * The basis is rebuilt through linalg.cameraMake, the same construction
 * camera.c uses, so the camera the shader receives from the editor is built
 * the way the camera the shader receives from a game is built.
 */
(function (root) {
    'use strict';

    var L = root.linalg;
    var UP_HINT = L.v3(0, 1, 0);

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
            state: function () {
                return { pos: pos, yaw: yaw, pitch: pitch, fovDeg: fovDeg };
            },
            setFov: function (deg) { fovDeg = deg; }
        };
    }

    root.camera = { create: create };
}(window.Hologram = window.Hologram || {}));
