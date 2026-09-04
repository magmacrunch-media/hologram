/* The room from above: where you can walk, and what you can see.
 *
 * The tracer cannot draw the collision boxes. They are not optical -- it has
 * no boxes and they reflect nothing -- so in the first-person view a wall is
 * invisible and the only evidence of it is being stopped. That is a poor way
 * to author one, and it is how crystal-mirror-maze's walls are authored
 * today: written as C, run, walked into.
 *
 * A plan is the natural drawing for this. Walls are axis-aligned boxes and a
 * top-down projection loses only their height, which is exactly what the
 * labels and the inspector carry. Drawn underneath them, faintly, are the
 * scene's panels -- because the whole point of a separate collision world is
 * that it does NOT match the geometry you can see, and the mistakes worth
 * catching are a wall that has drifted from its mirror, a doorway that
 * closed, a pane you can walk through.
 *
 * Nothing here derives a wall from a panel. That would quietly make them the
 * same surface, which the engine deliberately does not.
 */
(function (root) {
    'use strict';

    var HANDLE = 5;          /* corner grab radius, screen px */
    var MIN_SIZE = 0.02;     /* a wall may not be dragged inside out */

    function el(tag, cls, text) {
        var e = document.createElement(tag);
        if (cls) { e.className = cls; }
        if (text !== undefined) { e.textContent = text; }
        return e;
    }

    /* opts: { host, canvas, list, body, doc, walkDoc, camera, changed,
               selected } */
    function create(opts) {
        var canvas = document.getElementById(opts.canvas);
        var ctx = canvas.getContext('2d');
        var view = { cx: 0, cz: 0, scale: 30 };   /* world centre, px per m */
        var sel = -1;
        var drag = null;
        var fitted = false;

        var inspector = root.inspector.create({
            beginEdit: function () { opts.beginEdit(); },
            changed: function () {
                render();
                /* The row says what the wall IS -- "0.8 x 12.6, curb 0.4
                   high" -- so it goes stale the moment a corner moves.
                   Rebuilding the list cannot steal the caret here: the
                   inspector's inputs live in a different element. */
                renderList();
                opts.changed();
            },
            commit: function () { opts.commit(); }
        });

        function world() {
            var wd = opts.walkDoc();
            return wd ? wd.world : null;
        }

        function walls() {
            var w = world();
            return w ? (w.walls || (w.walls = [])) : [];
        }

        /* ---- transforms --------------------------------------------- */

        function sx(x) { return canvas.width / 2 + (x - view.cx) * view.scale; }
        function sz(z) { return canvas.height / 2 + (z - view.cz) * view.scale; }
        function wx(px) { return view.cx + (px - canvas.width / 2) / view.scale; }
        function wz(pz) { return view.cz + (pz - canvas.height / 2) / view.scale; }

        function fit() {
            var b = null;
            function grow(x, z) {
                if (!b) { b = { x0: x, x1: x, z0: z, z1: z }; return; }
                b.x0 = Math.min(b.x0, x); b.x1 = Math.max(b.x1, x);
                b.z0 = Math.min(b.z0, z); b.z1 = Math.max(b.z1, z);
            }
            walls().forEach(function (w) {
                grow(w.min[0], w.min[2]); grow(w.max[0], w.max[2]);
            });
            (opts.doc().rects || []).forEach(function (r) {
                var c = r.corner, u = r.edge_u, v = r.edge_v;
                grow(c[0], c[2]);
                grow(c[0] + u[0] + v[0], c[2] + u[2] + v[2]);
            });
            (opts.doc().spheres || []).forEach(function (s) {
                grow(s.center[0] - s.radius, s.center[2] - s.radius);
                grow(s.center[0] + s.radius, s.center[2] + s.radius);
            });
            if (!b) { b = { x0: -5, x1: 5, z0: -5, z1: 5 }; }
            var pad = 1.2;
            view.cx = (b.x0 + b.x1) / 2;
            view.cz = (b.z0 + b.z1) / 2;
            var sxs = canvas.width / Math.max(0.5, (b.x1 - b.x0) + pad * 2);
            var szs = canvas.height / Math.max(0.5, (b.z1 - b.z0) + pad * 2);
            view.scale = Math.max(4, Math.min(sxs, szs));
        }

        /* ---- drawing -------------------------------------------------- */

        function css(name, fallback) {
            var v = getComputedStyle(canvas).getPropertyValue(name).trim();
            return v || fallback;
        }

        function render() {
            var w = world();
            var ink = css('color', '#000');
            canvas.width = canvas.clientWidth * (window.devicePixelRatio || 1);
            canvas.height = canvas.clientHeight * (window.devicePixelRatio || 1);
            if (!fitted && w) { fit(); fitted = true; }

            ctx.clearRect(0, 0, canvas.width, canvas.height);
            ctx.lineJoin = 'miter';

            /* One-metre grid, the same square the checker floor uses. */
            var step = view.scale < 12 ? 5 : 1;
            ctx.strokeStyle = ink;
            ctx.globalAlpha = 0.10;
            ctx.lineWidth = 1;
            ctx.beginPath();
            var x0 = Math.floor(wx(0) / step) * step, x1 = wx(canvas.width);
            for (var gx = x0; gx <= x1; gx += step) {
                ctx.moveTo(sx(gx), 0); ctx.lineTo(sx(gx), canvas.height);
            }
            var z0 = Math.floor(wz(0) / step) * step, z1 = wz(canvas.height);
            for (var gz = z0; gz <= z1; gz += step) {
                ctx.moveTo(0, sz(gz)); ctx.lineTo(canvas.width, sz(gz));
            }
            ctx.stroke();
            ctx.globalAlpha = 1;

            /* The optical geometry, faint and underneath: a wall that has
               drifted from the mirror it is meant to back is the mistake
               this whole view exists to make visible. */
            ctx.strokeStyle = ink;
            ctx.globalAlpha = 0.45;
            ctx.lineWidth = 2;
            ctx.beginPath();
            (opts.doc().rects || []).forEach(function (r) {
                var c = r.corner, u = r.edge_u, v = r.edge_v;
                /* A panel's footprint is the longer of its two edges laid
                   flat; a wall-shaped rect has one edge vertical. */
                var ax = c[0], az = c[2];
                var bx = c[0] + u[0] + v[0], bz = c[2] + u[2] + v[2];
                ctx.moveTo(sx(ax), sz(az));
                ctx.lineTo(sx(bx), sz(bz));
            });
            ctx.stroke();
            ctx.globalAlpha = 0.35;
            (opts.doc().spheres || []).forEach(function (s) {
                ctx.beginPath();
                ctx.arc(sx(s.center[0]), sz(s.center[2]),
                        Math.max(1, s.radius * view.scale), 0, Math.PI * 2);
                ctx.stroke();
            });
            ctx.globalAlpha = 1;

            if (!w) {
                ctx.globalAlpha = 0.6;
                ctx.fillStyle = ink;
                ctx.font = (12 * (window.devicePixelRatio || 1)) + 'px monospace';
                ctx.fillText('no walk world for this scene',
                             12, 12 + 14 * (window.devicePixelRatio || 1));
                ctx.globalAlpha = 1;
                return;
            }

            /* The walls. Height cannot be drawn, so it is written: a curb
               and a wall are the same rectangle from up here. */
            walls().forEach(function (b, i) {
                var x = sx(Math.min(b.min[0], b.max[0]));
                var z = sz(Math.min(b.min[2], b.max[2]));
                var ww = Math.abs(b.max[0] - b.min[0]) * view.scale;
                var hh = Math.abs(b.max[2] - b.min[2]) * view.scale;
                var isSel = i === sel;
                var low = b.max[1] - b.min[1] < w.height * 0.6 || b.min[1] > 0.01;

                ctx.fillStyle = ink;
                ctx.globalAlpha = isSel ? 0.32 : (low ? 0.12 : 0.20);
                ctx.fillRect(x, z, Math.max(1, ww), Math.max(1, hh));
                ctx.globalAlpha = 1;
                ctx.strokeStyle = ink;
                ctx.lineWidth = isSel ? 2.5 : 1;
                ctx.setLineDash(low ? [5, 3] : []);
                ctx.strokeRect(x, z, Math.max(1, ww), Math.max(1, hh));
                ctx.setLineDash([]);

                if (isSel) {
                    ctx.fillStyle = ink;
                    [[x, z], [x + ww, z], [x, z + hh], [x + ww, z + hh]]
                        .forEach(function (p) {
                            ctx.fillRect(p[0] - HANDLE, p[1] - HANDLE,
                                         HANDLE * 2, HANDLE * 2);
                        });
                }
            });

            /* Where you are, and which way you are facing. */
            var cam = opts.camera();
            var st = cam.state();
            var px = sx(st.pos.x), pz = sz(st.pos.z);
            ctx.strokeStyle = ink;
            ctx.fillStyle = ink;
            ctx.globalAlpha = 0.9;
            ctx.beginPath();
            ctx.arc(px, pz, 4, 0, Math.PI * 2);
            ctx.fill();
            /* The walker's radius, so a doorway can be judged by eye. */
            ctx.globalAlpha = 0.35;
            ctx.beginPath();
            ctx.arc(px, pz, w.radius * view.scale, 0, Math.PI * 2);
            ctx.stroke();
            ctx.globalAlpha = 0.9;
            var fx = Math.sin(st.yaw), fz = -Math.cos(st.yaw);
            ctx.beginPath();
            ctx.moveTo(px, pz);
            ctx.lineTo(px + fx * 22, pz + fz * 22);
            ctx.stroke();
            ctx.globalAlpha = 1;
        }

        /* ---- hit testing ---------------------------------------------- */

        function cornerAt(px, pz) {
            if (sel < 0) { return null; }
            var b = walls()[sel];
            if (!b) { return null; }
            var xs = [b.min[0], b.max[0]], zs = [b.min[2], b.max[2]];
            for (var i = 0; i < 2; i++) {
                for (var j = 0; j < 2; j++) {
                    if (Math.abs(sx(xs[i]) - px) <= HANDLE * 1.6 &&
                        Math.abs(sz(zs[j]) - pz) <= HANDLE * 1.6) {
                        return { xi: i, zi: j };
                    }
                }
            }
            return null;
        }

        function wallAt(px, pz) {
            var list = walls();
            /* Topmost first, so overlapping boxes select the one drawn last. */
            for (var i = list.length - 1; i >= 0; i--) {
                var b = list[i];
                var x = wx(px), z = wz(pz);
                if (x >= Math.min(b.min[0], b.max[0]) &&
                    x <= Math.max(b.min[0], b.max[0]) &&
                    z >= Math.min(b.min[2], b.max[2]) &&
                    z <= Math.max(b.min[2], b.max[2])) {
                    return i;
                }
            }
            return -1;
        }

        function localPos(e) {
            var r = canvas.getBoundingClientRect();
            var dpr = window.devicePixelRatio || 1;
            return [(e.clientX - r.left) * dpr, (e.clientY - r.top) * dpr];
        }

        /* ---- interaction ---------------------------------------------- */

        canvas.addEventListener('pointerdown', function (e) {
            if (!world()) { return; }
            var p = localPos(e);
            canvas.setPointerCapture(e.pointerId);

            var corner = cornerAt(p[0], p[1]);
            if (corner) {
                opts.beginEdit();
                drag = { kind: 'corner', corner: corner };
                return;
            }
            var hit = wallAt(p[0], p[1]);
            if (hit >= 0) {
                if (hit !== sel) { select(hit); }
                opts.beginEdit();
                drag = { kind: 'move', x: wx(p[0]), z: wz(p[1]) };
                return;
            }
            /* Empty space: pan, and drop the selection. */
            if (sel !== -1) { select(-1); }
            drag = { kind: 'pan', x: p[0], z: p[1] };
        });

        canvas.addEventListener('pointermove', function (e) {
            var p = localPos(e);
            if (!drag) {
                canvas.style.cursor = cornerAt(p[0], p[1]) ? 'nwse-resize'
                    : (wallAt(p[0], p[1]) >= 0 ? 'move' : 'grab');
                return;
            }
            if (drag.kind === 'pan') {
                view.cx -= (p[0] - drag.x) / view.scale;
                view.cz -= (p[1] - drag.z) / view.scale;
                drag.x = p[0]; drag.z = p[1];
                render();
                return;
            }
            var b = walls()[sel];
            if (!b) { return; }
            if (drag.kind === 'move') {
                var dx = wx(p[0]) - drag.x, dz = wz(p[1]) - drag.z;
                b.min[0] += dx; b.max[0] += dx;
                b.min[2] += dz; b.max[2] += dz;
                drag.x = wx(p[0]); drag.z = wz(p[1]);
            } else {
                /* A corner drag edits the two coordinates it owns, and is
                   stopped from crossing the opposite face -- a box turned
                   inside out has no collision at all, which would look like
                   the wall simply vanishing. */
                var xKey = drag.corner.xi === 0 ? 'min' : 'max';
                var zKey = drag.corner.zi === 0 ? 'min' : 'max';
                var oppX = drag.corner.xi === 0 ? b.max[0] : b.min[0];
                var oppZ = drag.corner.zi === 0 ? b.max[2] : b.min[2];
                var nx = wx(p[0]), nz = wz(p[1]);
                b[xKey][0] = drag.corner.xi === 0
                    ? Math.min(nx, oppX - MIN_SIZE) : Math.max(nx, oppX + MIN_SIZE);
                b[zKey][2] = drag.corner.zi === 0
                    ? Math.min(nz, oppZ - MIN_SIZE) : Math.max(nz, oppZ + MIN_SIZE);
            }
            render();
            renderInspector();
            opts.changed();
        });

        function endDrag(e) {
            if (!drag) { return; }
            if (drag.kind !== 'pan') { opts.commit(); renderList(); }
            drag = null;
            if (e && canvas.hasPointerCapture(e.pointerId)) {
                canvas.releasePointerCapture(e.pointerId);
            }
        }
        canvas.addEventListener('pointerup', endDrag);
        canvas.addEventListener('pointercancel', endDrag);

        canvas.addEventListener('wheel', function (e) {
            e.preventDefault();
            var p = localPos(e);
            var before = [wx(p[0]), wz(p[1])];
            view.scale *= e.deltaY < 0 ? 1.15 : 1 / 1.15;
            view.scale = Math.max(2, Math.min(400, view.scale));
            /* Keep the point under the cursor still. */
            view.cx += before[0] - wx(p[0]);
            view.cz += before[1] - wz(p[1]);
            render();
        }, { passive: false });

        /* ---- list and inspector --------------------------------------- */

        function select(i) {
            sel = i;
            renderList();
            renderInspector();
            render();
            if (opts.selected) { opts.selected(i); }
        }

        function renderList() {
            var host = document.getElementById(opts.list);
            host.innerHTML = '';
            var w = world();
            if (!w) {
                host.appendChild(el('p', 'note',
                    'This scene has no walk world. Only the examples with a ' +
                    'walker dump one.'));
                return;
            }
            var list = walls();

            var head = el('div', 'lhead');
            head.appendChild(el('span', 'lname', 'walls'));
            head.appendChild(el('span', 'lcount',
                                list.length + ' / ' + root.schema.MAX_WALLS));
            var add = el('button', 'ladd', '+');
            if (list.length >= root.schema.MAX_WALLS) {
                add.disabled = true;
                add.title = 'HOLO_MAX_WALLS. Walls past this are not ' +
                            'collided against at all.';
            } else {
                add.title = 'add a wall at the middle of the view';
                add.addEventListener('click', function () {
                    opts.beginEdit();
                    list.push(root.schema.newWall({ x: view.cx, z: view.cz }));
                    opts.commit();
                    select(list.length - 1);
                    opts.changed();
                });
            }
            head.appendChild(add);
            host.appendChild(head);

            list.forEach(function (b, i) {
                var row = el('div', 'lrow' + (i === sel ? ' sel' : ''));
                row.appendChild(el('span', 'lidx', String(i)));
                row.appendChild(el('span', 'ldesc',
                                   root.schema.describeWall(b, w)));
                var del = el('button', 'ldel', '×');
                del.title = 'delete';
                del.addEventListener('click', function (ev) {
                    ev.stopPropagation();
                    opts.beginEdit();
                    list.splice(i, 1);
                    opts.commit();
                    if (sel >= list.length) { sel = list.length - 1; }
                    select(sel);
                    opts.changed();
                });
                row.appendChild(del);
                row.addEventListener('click', function () { select(i); });
                host.appendChild(row);
            });
        }

        function renderInspector() {
            var body = document.getElementById(opts.body);
            var w = world();
            if (!w) { body.innerHTML = ''; return; }
            if (sel < 0 || !walls()[sel]) {
                /* Nothing selected: the world's own fields, which are as
                   much a part of authoring as the boxes are. */
                body.innerHTML = '';
                body.appendChild(el('h3', 'subhead', 'world'));
                var into = el('div');
                body.appendChild(into);
                inspector.fields(into, w, root.schema.WALK_WORLD);
                return;
            }
            body.innerHTML = '';
            body.appendChild(el('h3', 'subhead', 'wall ' + sel));
            var box = el('div');
            body.appendChild(box);
            inspector.fields(box, walls()[sel], root.schema.WALL);
        }

        function refresh(refit) {
            if (refit) { fitted = false; }
            renderList();
            renderInspector();
            render();
        }

        return {
            refresh: refresh,
            render: render,
            select: select,
            selection: function () { return sel; },
            fit: function () { fitted = false; render(); }
        };
    }

    root.planview = { create: create };
}(window.Hologram = window.Hologram || {}));
