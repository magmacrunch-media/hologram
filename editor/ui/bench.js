/* The scene bench: what is in the room, what is selected, and what it is
 * made of.
 *
 * Owns the primitive list, selection, add / duplicate / delete, undo, and
 * the C the whole thing emits. The inspector panels come from
 * ui/inspector.js walking core/schema.js; nothing in here knows a sphere
 * from a dish beyond which table to hand over and where the middle of one
 * is for the camera to look at.
 *
 * Adding is refused at the cap rather than allowed and silently dropped by
 * the tracer. The caps are not advice -- see core/caps.js.
 */
(function (root) {
    'use strict';

    var L = root.linalg;

    function el(tag, cls, text) {
        var e = document.createElement(tag);
        if (cls) { e.className = cls; }
        if (text !== undefined) { e.textContent = text; }
        return e;
    }

    /* Where a primitive is, and how big, so the camera can frame it. */
    function extentOf(kind, o) {
        if (kind === 'sphere') {
            return { at: L.fromArray(o.center), size: o.radius || 0.5 };
        }
        if (kind === 'dish') {
            return { at: L.fromArray(o.apex), size: o.rim || 1 };
        }
        var c = L.fromArray(o.corner);
        var u = L.fromArray(o.edge_u), v = L.fromArray(o.edge_v);
        return {
            at: L.add(c, L.scale(L.add(u, v), 0.5)),
            size: Math.max(L.len(u), L.len(v)) * 0.5
        };
    }

    /* opts: { doc, ids, camera(), changed(), redraw() } */
    function create(opts) {
        var doc = opts.doc;
        var ids = opts.ids;
        var $ = function (id) { return document.getElementById(id); };

        var sel = null;              /* {list, index} or {scene:'floor'|'sky'} */
        var savedRevision = 0;

        /* Snapshot undo over the whole document. A scene is a few kilobytes
           of JSON -- the artstore problem magma-kit solved for megabyte
           sprite sheets does not arise here, so the simple thing is right. */
        /* One stack for everything, because Ctrl+Z means "undo the last
           thing I did" and a person moving between the scene list and the
           plan does not think of them as two documents. `extra` is the walk
           world, which lives in a different file and so cannot simply be a
           key of `doc`. */
        var history = window.MagmaKit.history.create({
            cap: 200,
            snapshot: function () {
                return JSON.stringify({
                    doc: doc,
                    extra: opts.extra ? opts.extra.get() : null
                });
            },
            restore: function (s) {
                var next = JSON.parse(s);
                Object.keys(doc).forEach(function (k) { delete doc[k]; });
                Object.keys(next.doc).forEach(function (k) {
                    doc[k] = next.doc[k];
                });
                if (opts.extra && next.extra) { opts.extra.set(next.extra); }
                clampSelection();
                renderAll();
                if (opts.restored) { opts.restored(); }
                opts.changed();
            }
        });

        var inspector = root.inspector.create({
            beginEdit: function () { history.beginStroke(); },
            changed: function () {
                renderList();
                /* Not renderInspector(): that would rebuild the input being
                   typed into. Only the inert decorations can have gone
                   stale, so only those are refreshed. */
                inspector.refreshInert($(ids.selBody));
                renderEmit();
                opts.changed();
            },
            commit: function () { history.commitStroke(); syncDirty(); }
        });

        function listOf(key) { return doc[key] || (doc[key] = []); }

        function clampSelection() {
            if (!sel || sel.scene) { return; }
            var items = listOf(sel.list);
            if (!items.length) { sel = null; }
            else if (sel.index >= items.length) { sel.index = items.length - 1; }
        }

        /* ---- the list ------------------------------------------------- */

        function renderList() {
            var host = $(ids.list);
            host.innerHTML = '';

            root.schema.LISTS.forEach(function (spec) {
                var items = listOf(spec.key);
                var caps = doc.caps || root.caps.DEFAULTS;
                var cap = caps[spec.cap];

                var head = el('div', 'lhead');
                head.appendChild(el('span', 'lname', spec.key));
                head.appendChild(el('span', 'lcount', items.length + ' / ' + cap));

                var add = el('button', 'ladd', '+');
                if (items.length >= cap) {
                    add.disabled = true;
                    add.title = root.caps.MEANING[spec.cap];
                } else {
                    add.title = 'add a ' + spec.label;
                    add.addEventListener('click', function () {
                        history.push();
                        items.push(root.schema.DEFAULTS[spec.kind]());
                        sel = { list: spec.key, index: items.length - 1 };
                        frameSelected();
                        renderAll();
                        opts.changed();
                        syncDirty();
                    });
                }
                head.appendChild(add);
                host.appendChild(head);

                items.forEach(function (o, i) {
                    var row = el('div', 'lrow' +
                        (sel && sel.list === spec.key && sel.index === i
                            ? ' sel' : ''));
                    row.appendChild(el('span', 'lidx', String(i)));
                    row.appendChild(el('span', 'ldesc',
                                       root.schema.describe(spec.kind, o)));

                    var del = el('button', 'ldel', '×');
                    del.title = 'delete';
                    del.addEventListener('click', function (e) {
                        e.stopPropagation();
                        history.push();
                        items.splice(i, 1);
                        clampSelection();
                        renderAll();
                        opts.changed();
                        syncDirty();
                    });
                    row.appendChild(del);

                    row.addEventListener('click', function () {
                        sel = { list: spec.key, index: i };
                        renderAll();
                    });
                    host.appendChild(row);
                });
            });

            /* The scene's own two groups sit at the bottom of the same list,
               because "the floor" and "the sky" are things you select and
               edit exactly like a panel. */
            [['floor', 'floor'], ['sky', 'sun and sky']].forEach(function (p) {
                var row = el('div', 'lrow scene' +
                    (sel && sel.scene === p[0] ? ' sel' : ''));
                row.appendChild(el('span', 'ldesc', p[1]));
                row.addEventListener('click', function () {
                    sel = { scene: p[0] };
                    renderAll();
                });
                host.appendChild(row);
            });
        }

        /* ---- the inspector -------------------------------------------- */

        function renderInspector() {
            var title = $(ids.selTitle), body = $(ids.selBody);
            var tools = $(ids.selTools);
            tools.innerHTML = '';

            if (!sel) {
                title.textContent = 'nothing selected';
                body.innerHTML = '';
                return;
            }

            if (sel.scene) {
                title.textContent = sel.scene === 'floor' ? 'floor' : 'sun and sky';
                doc[sel.scene] = doc[sel.scene] || {};
                inspector.fields(body, doc[sel.scene],
                    sel.scene === 'floor' ? root.schema.FLOOR : root.schema.SKY);
                return;
            }

            var spec = root.schema.LISTS.filter(function (s) {
                return s.key === sel.list;
            })[0];
            var obj = listOf(sel.list)[sel.index];
            if (!obj) { sel = null; return renderInspector(); }

            title.textContent = spec.label + ' ' + sel.index;

            var frame = el('button', null, 'look at');
            frame.title = 'point the camera at this  (F)';
            frame.addEventListener('click', function () {
                frameSelected();
                opts.changed();
            });
            tools.appendChild(frame);

            var dup = el('button', null, 'duplicate');
            var items = listOf(sel.list);
            var cap = (doc.caps || root.caps.DEFAULTS)[spec.cap];
            if (items.length >= cap) {
                dup.disabled = true;
                dup.title = root.caps.MEANING[spec.cap];
            } else {
                dup.addEventListener('click', function () {
                    history.push();
                    items.splice(sel.index + 1, 0,
                                 JSON.parse(JSON.stringify(obj)));
                    sel.index++;
                    renderAll();
                    opts.changed();
                    syncDirty();
                });
            }
            tools.appendChild(dup);

            inspector.fields(body, obj, spec.fields);
        }

        function frameSelected() {
            if (!sel || sel.scene) { return; }
            var spec = root.schema.LISTS.filter(function (s) {
                return s.key === sel.list;
            })[0];
            var obj = listOf(sel.list)[sel.index];
            if (!obj) { return; }
            var e = extentOf(spec.kind, obj);
            opts.camera().frame(e.at, e.size);
        }

        /* ---- the C ----------------------------------------------------- */

        function renderEmit() {
            var box = $(ids.emit);
            if (!box || box.hidden) { return; }
            var world = opts.extra ? opts.extra.get() : null;
            box.value = root.emit.toC(doc, 'scene') + '\n' +
                        (world ? root.emit.worldToC(world, 'world') + '\n' : '') +
                        root.emit.cameraToC(opts.camera());
        }

        /* ---- dirty ------------------------------------------------------ */

        function syncDirty() {
            var d = history.revision() !== savedRevision;
            $(ids.dirty).hidden = !d;
            $(ids.undo).disabled = !history.canUndo();
            $(ids.redo).disabled = !history.canRedo();
        }

        function renderAll() {
            renderList();
            renderInspector();
            renderEmit();
            syncDirty();
            /* The sweep panel offers the selection's fields, so it follows
               selection rather than polling for it. */
            if (opts.selected) { opts.selected(); }
        }

        /* ---- wiring ------------------------------------------------------ */

        $(ids.undo).addEventListener('click', function () {
            history.cancelStroke();
            history.undo();
            syncDirty();
        });
        $(ids.redo).addEventListener('click', function () {
            history.cancelStroke();
            history.redo();
            syncDirty();
        });

        var emitBox = $(ids.emit);
        $(ids.emitToggle).addEventListener('click', function () {
            emitBox.hidden = !emitBox.hidden;
            $(ids.emitCopy).hidden = emitBox.hidden;
            $(ids.emitToggle).textContent =
                emitBox.hidden ? 'show C' : 'hide C';
            renderEmit();
        });
        $(ids.emitCopy).addEventListener('click', function () {
            emitBox.select();
            navigator.clipboard.writeText(emitBox.value).then(function () {
                $(ids.emitCopy).textContent = 'copied';
                setTimeout(function () {
                    $(ids.emitCopy).textContent = 'copy';
                }, 1200);
            }).catch(function () {
                /* No clipboard permission: the textarea is selected, so
                   Ctrl+C still works and says so. */
                $(ids.emitCopy).textContent = 'press Ctrl+C';
            });
        });

        renderAll();

        return {
            render: renderAll,
            renderEmit: renderEmit,
            frameSelected: frameSelected,
            selection: function () { return sel; },
            /* The plan view drives the same stack, so a wall drag collapses
               to one undo the way a slider drag does. */
            beginWorldStroke: function () { history.beginStroke(); },
            commitWorldStroke: function () {
                history.commitStroke();
                syncDirty();
            },
            undo: function () {
                history.cancelStroke();
                if (history.undo()) { syncDirty(); }
            },
            redo: function () {
                history.cancelStroke();
                if (history.redo()) { syncDirty(); }
            },
            markSaved: function () {
                savedRevision = history.revision();
                syncDirty();
            },
            /* For a restored draft: the document differs from the file it
               was loaded from, and every panel that cares should know. */
            markDirty: function () {
                savedRevision = history.revision() - 1;
                syncDirty();
            },
            /* Swap in a different scene, keeping the SAME object -- every
               panel holds this one by reference, so replacing it wholesale
               would leave them all editing the document nobody can see. */
            replace: function (next, keepHistory) {
                if (!keepHistory) {
                    history.clear();
                    savedRevision = history.revision();
                } else {
                    history.push();
                }
                Object.keys(doc).forEach(function (k) { delete doc[k]; });
                Object.keys(next).forEach(function (k) { doc[k] = next[k]; });
                sel = null;
                renderAll();
            },
            isDirty: function () { return history.revision() !== savedRevision; }
        };
    }

    root.bench = { create: create, extentOf: extentOf };
}(window.Hologram = window.Hologram || {}));
