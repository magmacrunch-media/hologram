/* Clicking and dragging in the rendered view.
 *
 * Left button picks, and drags whatever it picked. Right button looks
 * around. That split is why pointer lock is gone: locking the cursor is the
 * right answer for a game and the wrong one for an editor, where the cursor
 * is the thing you point at objects with. Right-drag captures the pointer
 * instead, so a turn can run past the edge of the window without the modal
 * "click to look, Escape to release" dance the view used to need.
 *
 * A drag moves the primitive's ORIGIN -- a sphere's centre, a dish's apex, a
 * rect's corner. Moving a rect by its corner carries the whole panel because
 * its edges are relative to it, which is also why the corner is the only
 * handle a panel needs.
 *
 * The plane a drag happens in is the horizontal one through the object,
 * because rooms are laid out on a floor and that is the motion you almost
 * always want. Shift swaps to the vertical plane facing the camera, which is
 * how something gets raised. Both are ray-plane intersections against the
 * grab point, so the object stays under the cursor rather than drifting from
 * it at distance.
 */
(function (root) {
    'use strict';

    var L = root.linalg;

    /* Which field of a primitive is its position. */
    var ORIGIN = { sphere: 'center', rect: 'corner', dish: 'apex' };
    var KIND_OF = {};
    KIND_OF[root.pick.SPHERE] = { list: 'spheres', kind: 'sphere' };
    KIND_OF[root.pick.RECT] = { list: 'rects', kind: 'rect' };
    KIND_OF[root.pick.DISH] = { list: 'dishes', kind: 'dish' };

    function create(opts) {
        var canvas = document.getElementById(opts.canvas);
        var drag = null;
        var looking = null;

        /* The ray through a pixel of the canvas, in the camera the view is
           currently rendering with. */
        function rayAt(e) {
            var r = canvas.getBoundingClientRect();
            var u = (e.clientX - r.left) / r.width;
            var v = (e.clientY - r.top) / r.height;
            var cam = opts.camera().basis(opts.aspect());
            return root.pick.cameraRay(cam, u, v);
        }

        /* Where a ray meets the drag plane, through `at`.
         *
           A horizontal drag wants the ground plane, and that is the right
           plane right up until you are looking along it -- which is the
           usual way to stand in a room. m7_room's camera is exactly level,
           so its rays are parallel to y = const and never meet it, and the
           drag would simply do nothing in the view people spend most of
           their time in.

           So when the ray is within a few degrees of grazing, the plane
           swaps to the one facing the camera. The move handler only takes
           the coordinates it wants out of the result either way, so the
           object still travels in x and z; what changes is that there is an
           intersection to take them from. */
        function meet(ray, at, n) {
            var denom = L.dot(ray.dir, n);
            if (Math.abs(denom) < 1e-6) { return null; }
            var t = L.dot(L.sub(at, ray.origin), n) / denom;
            if (t <= 0) { return null; }
            return L.add(ray.origin, L.scale(ray.dir, t));
        }

        function planePoint(ray, at, vertical) {
            var fwd = opts.camera().basis(opts.aspect()).forward;
            /* Facing the camera, so it always has an intersection in front:
               the object is on it, and the object is what you clicked. */
            var screen = L.norm(L.v3(-fwd.x, -fwd.y, -fwd.z));
            var preferred = vertical
                ? L.norm(L.v3(fwd.x, 0, fwd.z))
                : L.v3(0, 1, 0);
            if (L.len(preferred) < 1e-4) { preferred = screen; }
            /* The fallback is on FAILURE, not on a predicted condition. The
               ground plane through an object is missed two ways -- a level
               view runs parallel to it, and a view angled slightly up puts
               it behind the ray when the object sits below eye level, which
               is most objects in most rooms. Both come back as no
               intersection, so both are answered the same way. */
            return meet(ray, at, preferred) || meet(ray, at, screen);
        }

        function originOf(kind, obj) {
            return L.fromArray(obj[ORIGIN[kind]]);
        }

        canvas.addEventListener('contextmenu', function (e) {
            e.preventDefault();
        });

        /* Capture only keeps events coming once the cursor leaves the
           canvas -- a convenience, not the mechanism. Some pointer ids are
           refused, and a throw here would take the whole drag with it. */
        function capture(e) {
            try { canvas.setPointerCapture(e.pointerId); } catch (err) { /* fine */ }
        }
        function release(e) {
            try {
                if (canvas.hasPointerCapture(e.pointerId)) {
                    canvas.releasePointerCapture(e.pointerId);
                }
            } catch (err) { /* fine */ }
        }

        canvas.addEventListener('pointerdown', function (e) {
            capture(e);

            if (e.button === 2 || e.button === 1) {
                looking = true;
                canvas.style.cursor = 'grabbing';
                e.preventDefault();
                return;
            }
            if (e.button !== 0) { return; }

            var ray = rayAt(e);
            var hit = root.pick.pick(opts.doc(), ray.origin, ray.dir);
            var meta = KIND_OF[hit.kind];
            if (!meta) {
                /* Floor or sky: a click on nothing clears the selection,
                   which is what makes the panel's state predictable. */
                opts.select(null);
                return;
            }
            var obj = opts.doc()[meta.list][hit.index];
            opts.select({ list: meta.list, index: hit.index });

            var at = originOf(meta.kind, obj);
            var grab = planePoint(ray, at, e.shiftKey);
            if (!grab) { return; }
            drag = {
                kind: meta.kind, obj: obj, vertical: e.shiftKey,
                /* The offset from the grab point to the origin, so the
                   object does not jump to the cursor on the first move. */
                offset: L.sub(at, grab),
                moved: false
            };
        });

        canvas.addEventListener('pointermove', function (e) {
            if (looking) {
                opts.camera().look(e.movementX * 0.0025, e.movementY * 0.0025);
                opts.changed();
                return;
            }
            if (!drag) { return; }
            var ray = rayAt(e);
            var at = originOf(drag.kind, drag.obj);
            var p = planePoint(ray, at, drag.vertical);
            if (!p) { return; }
            var next = L.add(p, drag.offset);

            if (!drag.moved) {
                /* The stroke opens on the first actual movement, so a click
                   that only selects does not land an empty undo entry. */
                opts.beginEdit();
                drag.moved = true;
            }
            var key = ORIGIN[drag.kind];
            if (drag.vertical) {
                drag.obj[key][1] = next.y;
            } else {
                drag.obj[key][0] = next.x;
                drag.obj[key][2] = next.z;
            }
            opts.edited();
        });

        function end(e) {
            if (looking) {
                looking = null;
                canvas.style.cursor = '';
            }
            if (drag) {
                if (drag.moved) { opts.commit(); }
                drag = null;
            }
            if (e) { release(e); }
        }
        canvas.addEventListener('pointerup', end);
        canvas.addEventListener('pointercancel', end);

        return {
            isDragging: function () { return !!drag || !!looking; }
        };
    }

    root.drag = { create: create };
}(window.Hologram = window.Hologram || {}));
