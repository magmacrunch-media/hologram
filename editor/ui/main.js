/* Wiring: load a dumped scene, pack it, render it, and say whether the
 * packing agrees with the C that dumped it.
 *
 * The editor lives one directory below the repository root and reads the
 * engine's own files in place -- shaders/trace.glsl as the tracer, and
 * build/<name>_{scene.json,params.bin} as a scene and the block C packed
 * from it. Nothing is copied or generated for the editor's benefit, which
 * is what keeps it from drifting into showing its own idea of the scene.
 */
(function (root) {
    'use strict';

    var ROOT = '..';
    var DESIGN_W = 640, DESIGN_H = 480;   /* the engine's design resolution */

    /* Where the dumped scene, its references and its walk world are read
       from. The engine's own build/ by default; ?dir= points somewhere else,
       which is how a game's rooms are opened -- crystal-mirror-maze dumps
       the First Hall into its own repository, and serve.py --mount exposes
       that directory rather than anyone copying files across. A copy goes
       stale the next time the game is rebuilt, and nothing would say so.
     *
       Only the dumps move. The tracer is always the engine's shaders/, which
       is the point: a game's shaders/ is a copy the build script made, and
       the editor should show what the oracle holds to the CPU reference. */
    var DUMPS = ROOT + '/build';

    var $ = function (id) { return document.getElementById(id); };

    var state = {
        name: null,
        doc: null,
        view: null,
        cam: null,
        params: null,
        spectral: false,
        held: {},
        running: false,
        lastFrame: 0,
        fps: 0,
        bench: null
    };

    function fail(where, err) {
        var box = $('error');
        box.hidden = false;
        box.textContent = where + ': ' + (err && err.message ? err.message : err);
        console.error(where, err);
    }

    function fetchText(path) {
        return fetch(path, { cache: 'no-store' }).then(function (r) {
            if (!r.ok) {
                throw new Error('cannot read ' + path + ' (' + r.status + ')');
            }
            return r.text();
        });
    }

    function fetchBuffer(path) {
        return fetch(path, { cache: 'no-store' }).then(function (r) {
            if (!r.ok) {
                throw new Error('cannot read ' + path + ' (' + r.status + ')');
            }
            return r.arrayBuffer();
        });
    }

    /* ---- the conformance banner ------------------------------------- */

    /* Pack the scene exactly as it was dumped -- its own camera, its own
       spectral flag -- and hold the result to the block C wrote. This is the
       check that makes core/scene.js trustworthy; see its header. */
    function runConformance(golden) {
        /* The scene AS FETCHED, not the one being edited. This is a check on
           the packer, and it must go on meaning that after an edit or a
           restored draft -- packing something else and comparing it to
           params.bin would report a packer failure about a different room. */
        var doc = state.pristine;
        var cam = doc.camera;
        var camBasis = {
            pos: root.linalg.fromArray(cam.pos),
            forward: root.linalg.fromArray(cam.forward),
            right: root.linalg.fromArray(cam.right),
            up: root.linalg.fromArray(cam.up),
            tan_half_fov: cam.tan_half_fov,
            aspect: cam.aspect
        };
        var packed = root.scene.pack(doc, camBasis, doc.spectral);
        return root.scene.conformance(new Float32Array(golden), packed);
    }

    /* A document opened from a file, or one whose dump the editor has since
       edited, has no params.bin or ref.bin belonging to it. Say so once,
       loudly, rather than letting two panels report confident verdicts about
       a room that is not this one. */
    function isDetached() {
        return !!(state.doc && state.doc.editor && state.doc.editor.edited) ||
               state.fromFile;
    }

    function showConformance(c) {
        var el = $('banner');
        el.classList.remove('ok', 'bad');
        if (!c) {
            el.classList.add('bad');
            el.textContent = 'PACKER UNCHECKED: no params.bin for this scene ' +
                '-- run  build\\' + state.name + '.exe --dump';
            return;
        }
        el.classList.add(c.ok ? 'ok' : 'bad');
        if (!c.sizeMatch) {
            el.textContent = 'PACKER FAIL: block is ' + c.packedFloats +
                ' floats, C wrote ' + c.goldenFloats +
                ' -- HoloGpuScene and core/scene.js have drifted';
            return;
        }
        el.textContent = 'PACKER ' + (c.ok ? 'OK' : 'FAIL') + ': ' +
            c.identical + ' of ' + c.compared + ' floats bit-identical to C' +
            (c.differing === 0
                ? ''
                : ', ' + c.differing + ' within rounding' +
                  (c.failures ? ', ' + c.failures + ' NOT' : '') +
                  ' (worst ' + c.worstField + ' by ' +
                  c.worst.abs.toExponential(2) + ')');

        /* Which fields differ is the diagnostic, not the headline number.
           Only computed fields can differ at all -- the CIE weights and the
           filter and groove axes. Any other name appearing in this list is
           a packing bug, whether or not it passed the tolerance. */
        var rows = Object.keys(c.byField).map(function (name) {
            var e = c.byField[name];
            return '<tr class="' + (e.failed ? 'over' : '') + '"><td>' + name +
                '</td><td class="num">' + e.count +
                '</td><td class="note">' + e.abs.toExponential(1) +
                (e.failed ? ' OVER' : '') + '</td></tr>';
        }).join('');
        $('packer-detail').innerHTML = rows ||
            '<tr><td colspan="3" class="note">every float identical to C</td></tr>';
    }

    /* ---- the walk world ----------------------------------------------- */

    /* Once the walls have been moved the dumped trace no longer describes
       this world, and the line reporting it should stop implying otherwise.
       The selftest is untouched -- its world is synthetic and no edit can
       reach it -- so the twin remains under check, and the wording says
       which of the two is still speaking. */
    function markWorldEdited() {
        if (!state.walkPristine || !state.walkDoc) { return; }
        var changed = JSON.stringify(state.walkDoc.world) !==
                      JSON.stringify(state.walkPristine.world);
        if (changed !== state.worldEdited) {
            state.worldEdited = changed;
            renderWalkCheck();
        }
    }

    /* ---- files -------------------------------------------------------- */

    function status(msg, bad) {
        var el = $('file-status');
        el.textContent = msg || '';
        el.className = 'note' + (bad ? ' bad' : '');
    }

    function doSave(forceAsk) {
        state.files.save(forceAsk).then(function (msg) {
            status(msg);
            state.bench.markSaved();
            /* The file is the record now; the draft would only resurrect an
               older version on the next load. */
            root.save.clearDraft(state.name);
            $('file-name').textContent = state.files.fileName() || '';
        }).catch(function (e) {
            /* Dismissing the picker is a decision, not a failure. */
            if (e && e.name === 'AbortError') { status(''); return; }
            status('save failed: ' + (e && e.message ? e.message : e), true);
        });
    }

    function doOpen() {
        state.files.open().then(function (r) {
            /* Reopening replaces the document but not the references: this
               file may be nothing like whatever was last dumped. The doc
               object itself is kept -- every panel holds it by reference. */
            state.fromFile = true;
            state.spectral = !!r.doc.spectral;
            $('spectral').checked = state.spectral;
            state.bench.replace(r.doc);
            state.cam = root.camera.create(state.doc);
            state.cam.setWorld(state.walkDoc);
            $('file-name').textContent = r.name;
            $('scene-name').textContent = r.name;
            document.title = 'hologram — ' + r.name;
            status('opened ' + r.name);
            markDetached();
            showBudget();
            invalidate();
        }).catch(function (e) {
            if (e && e.name === 'AbortError') { status(''); return; }
            status('open failed: ' + (e && e.message ? e.message : e), true);
        });
    }

    function markDetached() {
        var el = $('detached');
        el.hidden = !isDetached();
        if (!el.hidden) {
            el.textContent = 'This document did not come from the dump ' +
                'beside it. The packer and oracle checks refer to ' +
                'build/' + state.name + '_*.bin, which is a render of ' +
                'whatever was last dumped -- not of this.';
        }
    }

    /* Every edit writes a draft, so a closed tab does not lose work that was
       never saved to a file. Debounced: dragging a slider is one draft, not
       two hundred writes to localStorage. */
    var draftTimer = 0;
    function scheduleDraft() {
        if (draftTimer) { clearTimeout(draftTimer); }
        draftTimer = setTimeout(function () {
            draftTimer = 0;
            if (state.bench && state.bench.isDirty()) {
                root.save.saveDraft(state.name, state.doc);
            }
        }, 600);
    }

    /* ---- walking ------------------------------------------------------ */

    function setWalking(on) {
        var ok = state.cam.setWalking(on);
        $('walk').checked = ok;
        $('walk-row').classList.toggle('off', !ok);
        invalidate();
        return ok;
    }

    /* core/collision.js is a twin of holo_walk_step, and unlike the packer
       nothing about the picture would show it drifting -- a room you can
       walk through the wall of still renders correctly. So it is checked
       against traces the engine dumped: the example's own world, and a
       synthetic one built to be awkward about the cases a room does not
       exercise. See source/walk_json.h.
     *
       Both are replayed against the world AS DUMPED. A trace is a record of
       the C stepping a particular world, so editing the walls would make the
       replay diverge and report a broken twin when nothing about the
       arithmetic had changed. The selftest is the reason editing stays safe:
       its world is synthetic and no edit can reach it, so the twin goes on
       being checked however much the room is rearranged. */
    function showWalkConformance(walkDoc, selfDoc) {
        state.walkParts = [];
        state.walkOk = true;

        [[walkDoc, state.name], [selfDoc, 'selftest']].forEach(function (pair) {
            if (!pair[0]) { return; }
            var c = root.collision.conformance(pair[0]);
            if (!c.ok) { state.walkOk = false; }
            state.walkParts.push({
                name: pair[1],
                fromDump: pair[1] !== 'selftest',
                text: c.ok
                    ? c.exact + '/' + c.steps + ' bit-identical'
                    : c.differing + '/' + c.steps + ' differ, worst ' +
                      c.worst.d.toExponential(2) + ' at step ' + c.worst.step +
                      (c.groundedMismatch ? ', ' + c.groundedMismatch +
                       ' grounded' : '')
            });
        });
        renderWalkCheck();
    }

    function renderWalkCheck() {
        var el = $('walk-check');
        if (!state.walkParts || !state.walkParts.length) {
            el.textContent = 'no walk dump for this scene';
            el.className = 'note';
            return;
        }
        var parts = state.walkParts.map(function (p) {
            /* An edited world's trace was recorded from a different room, so
               its verdict is withdrawn rather than restated. */
            if (p.fromDump && state.worldEdited) {
                return p.name + ': world edited, trace withdrawn';
            }
            return p.name + ': ' + p.text;
        });
        el.textContent = (state.walkOk ? 'WALK OK  ' : 'WALK FAIL  ') +
                         parts.join('   ');
        el.className = state.walkOk ? 'note ok' : 'note bad';
    }

    /* ---- the budget panel -------------------------------------------- */

    function showBudget() {
        var rows = root.caps.budget(state.doc);
        var out = '';
        rows.forEach(function (r) {
            var cls = r.over ? 'over' : (r.full ? 'full' : '');
            out += '<tr class="' + cls + '" title="' + r.meaning + '">' +
                '<td>' + r.key.replace('gpu_', 'gpu ') + '</td>' +
                '<td class="num">' + r.used + '</td><td class="sep">/</td>' +
                '<td class="num">' + r.cap + '</td>' +
                '<td class="note">' + (r.over ? 'OVER' : (r.full ? 'full' : '')) +
                '</td></tr>';
        });
        $('budget').innerHTML = out;

        /* A third grating is the quiet one: the CPU renders it and the GPU
           does not, so it survives a CPU render and ships wrong. */
        var g = rows.filter(function (r) { return r.key === 'gpu_gratings'; })[0];
        var warn = $('grating-warning');
        warn.hidden = !(g && g.over);
        if (g && g.over) {
            warn.textContent = g.used + ' gratings, GPU holds ' + g.cap +
                '. The extras render MATTE BLACK on the GPU while the CPU ' +
                'oracle renders them correctly.';
        }
    }

    /* ---- drawing ------------------------------------------------------ */

    /* Rendered on demand, not on a permanent loop. A scene nobody is
       touching is a still image, and a fullscreen ray tracer is an
       expensive way to redraw one sixty times a second. Anything that
       changes what should be on screen calls invalidate(); holding a
       movement key keeps invalidating, which is what makes flying smooth.
     *
     * It also means the page cannot be left dead by a dropped animation
     * frame -- returning to the window schedules one, rather than hoping an
     * unbroken chain of requestAnimationFrame callbacks survived being in
     * the background. */
    var pendingFrame = 0;

    function moving() {
        if (state.held.w || state.held.a || state.held.s ||
            state.held.d || state.held.q || state.held.e) {
            return true;
        }
        if (!state.cam || !state.cam.isWalking() || !state.cam.walker()) {
            return false;
        }
        /* Airborne: gravity is still moving you with nothing held.
         *
           And jump-held, which is not the same thing and is easy to miss:
           standing still, a Space press schedules exactly one frame, and
           that frame carries dt = 0 because the loop had gone idle. Zero
           buys no fixed steps, so the impulse is never applied and the jump
           silently does nothing. Staying awake while the key is down gives
           the accumulator a real delta to work with. */
        return !state.cam.walker().grounded || !!state.held.jump;
    }

    function invalidate() {
        if (pendingFrame || !state.running) {
            return;
        }
        pendingFrame = requestAnimationFrame(frame);
    }

    /* A requested frame that never arrives would otherwise wedge the view
       shut for good: invalidate() would go on seeing a request already in
       flight and decline to make another. Browsers do drop them -- a tab
       backgrounded between the request and the callback is the usual way --
       so the paths that recover from being away cancel first and ask again,
       rather than trusting a handle from before. */
    function forceFrame() {
        if (pendingFrame) {
            cancelAnimationFrame(pendingFrame);
            pendingFrame = 0;
        }
        invalidate();
    }

    function frame(now) {
        pendingFrame = 0;
        if (!state.running) {
            return;
        }

        /* Clamped: coming back from a background tab hands over a gap of
           seconds, which would otherwise teleport the camera. timestep.c
           drops the same backlog for the same reason. */
        var dt = state.lastFrame ? Math.min((now - state.lastFrame) / 1000, 0.1) : 0;
        state.lastFrame = now;

        if (state.cam.isWalking()) {
            /* The real elapsed time goes to the accumulator, which decides
               how many fixed steps that buys -- the examples' arrangement,
               so the editor and the game agree about where you end up. */
            state.cam.walkFrame(dt, state.held);
            if (dt > 0) { state.fps = state.fps * 0.9 + (1 / dt) * 0.1; }
        } else if (moving() && dt > 0) {
            var speed = (state.held.shift ? 8 : 2.5) * dt;
            state.cam.move((state.held.d ? 1 : 0) - (state.held.a ? 1 : 0),
                           (state.held.e ? 1 : 0) - (state.held.q ? 1 : 0),
                           (state.held.w ? 1 : 0) - (state.held.s ? 1 : 0),
                           speed);
            state.fps = state.fps * 0.9 + (1 / dt) * 0.1;
        }

        var basis = state.cam.basis(DESIGN_W / DESIGN_H);
        state.params = root.scene.pack(state.doc, basis, state.spectral);
        state.view.draw(state.params, now / 1000);

        var s = state.cam.state();
        var w = state.cam.isWalking() ? state.cam.walker() : null;
        $('readout').textContent =
            (w ? 'walking  feet ' + w.pos.x.toFixed(2) + ' ' +
                 w.pos.y.toFixed(2) + ' ' + w.pos.z.toFixed(2) +
                 (w.grounded ? '' : '  airborne')
               : 'flying   pos ' + s.pos.x.toFixed(2) + ' ' +
                 s.pos.y.toFixed(2) + ' ' + s.pos.z.toFixed(2)) +
            '   yaw ' + (s.yaw * 180 / Math.PI).toFixed(0) + '°' +
            '   pitch ' + (s.pitch * 180 / Math.PI).toFixed(0) + '°' +
            '   fov ' + s.fovDeg.toFixed(0) + '°' +
            (moving() ? '   ' + state.fps.toFixed(0) + ' fps' : '   idle');

        if (moving()) {
            invalidate();
        } else {
            state.lastFrame = 0;
            /* The emitted C carries the camera, so it is refreshed when
               flying stops rather than on every frame of the flight. */
            if (state.bench) {
                state.bench.renderEmit();
            }
        }
    }

    /* ---- input -------------------------------------------------------- */

    var KEYS = { KeyW: 'w', KeyA: 'a', KeyS: 's', KeyD: 'd', KeyQ: 'q', KeyE: 'e' };

    function wireInput(canvas) {
        window.addEventListener('keydown', function (e) {
            if (e.target && /^(INPUT|SELECT|TEXTAREA)$/.test(e.target.tagName)) {
                return;
            }
            if (KEYS[e.code]) {
                state.held[KEYS[e.code]] = true;
                e.preventDefault();
                invalidate();
            }
            if (e.code === 'ShiftLeft' || e.code === 'ShiftRight') {
                state.held.shift = true;
            }
            if (e.code === 'Space') {
                state.held.jump = true;
                e.preventDefault();
                invalidate();
            }
            if (e.code === 'KeyG' && state.cam.canWalk()) {
                setWalking(!state.cam.isWalking());
            }
            if (e.code === 'KeyR') {
                state.cam.reset();
                invalidate();
            }
            if (e.code === 'KeyP') {
                state.spectral = !state.spectral;
                $('spectral').checked = state.spectral;
                invalidate();
            }
            if (e.code === 'KeyF' && state.bench) {
                state.bench.frameSelected();
                invalidate();
            }
            /* Undo/redo. Ctrl also means Cmd, the way MagmaKit.keys reads
               a binding table. */
            if ((e.ctrlKey || e.metaKey) && e.code === 'KeyZ' && state.bench) {
                if (e.shiftKey) { state.bench.redo(); } else { state.bench.undo(); }
                e.preventDefault();
            }
            if ((e.ctrlKey || e.metaKey) && e.code === 'KeyY' && state.bench) {
                state.bench.redo();
                e.preventDefault();
            }
            if ((e.ctrlKey || e.metaKey) && e.code === 'KeyS') {
                e.preventDefault();
                doSave(e.shiftKey);
            }
            if ((e.ctrlKey || e.metaKey) && e.code === 'KeyO') {
                e.preventDefault();
                doOpen();
            }
        });
        window.addEventListener('keyup', function (e) {
            if (KEYS[e.code]) {
                state.held[KEYS[e.code]] = false;
            }
            if (e.code === 'ShiftLeft' || e.code === 'ShiftRight') {
                state.held.shift = false;
            }
            if (e.code === 'Space') { state.held.jump = false; }
        });
        /* A window that loses focus mid-stride would otherwise keep walking;
           regaining it schedules a frame, so the view is never left stale. */
        window.addEventListener('blur', function () { state.held = {}; });
        window.addEventListener('focus', forceFrame);
        document.addEventListener('visibilitychange', function () {
            if (!document.hidden) {
                forceFrame();
            }
        });

        /* Looking and picking both want the mouse, so they take a button
           each -- see ui/drag.js for why pointer lock is gone. */
        $('lock-hint').textContent =
            'left-click selects and drags · right-drag looks · shift drags up';

        $('spectral').addEventListener('change', function (e) {
            state.spectral = e.target.checked;
            invalidate();
        });
        $('walk').addEventListener('change', function (e) {
            setWalking(e.target.checked);
        });
        $('save').addEventListener('click', function () { doSave(false); });
        $('save-as').addEventListener('click', function () { doSave(true); });
        $('open').addEventListener('click', doOpen);
        $('discard-draft').addEventListener('click', function () {
            root.save.clearDraft(state.name);
            location.reload();
        });
        $('plan-fit').addEventListener('click', function () {
            state.plan.fit();
        });
        $('cost-run').addEventListener('click', function () {
            var btn = $('cost-run'), st = $('cost-status');
            btn.disabled = true;
            st.textContent = 'measuring…';
            var frames = parseInt($('cost-frames').value, 10);
            state.benchPanel.run(st, frames > 0 ? frames : 40).then(function () {
                st.textContent = '';
                btn.disabled = false;
            }).catch(function (e) {
                st.textContent = 'failed: ' + (e && e.message ? e.message : e);
                st.className = 'note bad';
                btn.disabled = false;
            });
        });
        $('save-walls').addEventListener('click', function () {
            if (!state.walkDoc) { return; }
            var body = root.save.walkToJson(state.walkDoc.world,
                                            state.walkDoc.start, state.name);
            /* A separate file, because the engine keeps them separate:
               scene_json.c and walk_json.c write different documents and a
               game declares the two structs apart. */
            root.files.writeOut(state.name + '.walk.json', body)
                .then(function (msg) { status(msg); })
                .catch(function (e) {
                    if (e && e.name === 'AbortError') { status(''); return; }
                    status('save failed: ' + (e && e.message ? e.message : e),
                           true);
                });
        });

        /* An unsaved document should not vanish without a word. The draft is
           already written, so this is a courtesy, and browsers only honour
           it when the page has been interacted with. */
        window.addEventListener('beforeunload', function (e) {
            if (state.bench && state.bench.isDirty()) {
                e.preventDefault();
                e.returnValue = '';
            }
        });
        $('fov').addEventListener('input', function (e) {
            state.cam.setFov(parseFloat(e.target.value));
            $('fov-value').textContent = e.target.value + '°';
            invalidate();
        });
        $('reload-shader').addEventListener('click', function () {
            /* no-store, so this reads what is on disk now: edit the tracer,
               press the button, see it. display.c reads the same file at
               startup for the same reason. */
            fetchText(ROOT + '/shaders/trace.glsl').then(function (src) {
                state.view.setShader(src);
                state.shaderSource = src;
                $('error').hidden = true;
                invalidate();
                /* The oracle and sweep panels each compile their own copy;
                   drop both so the next run picks up what was just loaded. */
                if (state.oracle) { state.oracle.reset(); }
                if (state.sweep) { state.sweep.reset(); }
                if (state.benchPanel) { state.benchPanel.reset(); }
            }).catch(function (e) { fail('shader reload', e); });
        });

        var oracleBox = $('oracle');
        $('oracle-toggle').addEventListener('click', function () {
            oracleBox.hidden = !oracleBox.hidden;
            $('oracle-toggle').textContent =
                oracleBox.hidden ? 'oracle' : 'hide oracle';
            if (!oracleBox.hidden) {
                state.oracle.run();
            }
        });
    }

    /* ---- boot ---------------------------------------------------------- */

    function boot() {
        var params = new URLSearchParams(location.search);
        state.name = params.get('s') || 'm7_room';
        /* Trailing slash trimmed so ?dir=/mount/cmm and ?dir=/mount/cmm/ are
           the same place rather than two with a doubled separator. */
        var dir = (params.get('dir') || '').replace(/\/+$/, '');
        if (dir) { DUMPS = dir; }
        state.dumps = DUMPS;

        $('scene-name').textContent = state.name;
        document.title = 'hologram — ' + state.name;
        /* Where this room came from, shown whenever it is not the engine's
           own build/. Editing the wrong copy of a room is the failure this
           whole arrangement exists to avoid, so the answer is on screen
           rather than in the URL bar. */
        if (dir) {
            $('dumps-from').textContent = 'scenes from ' + dir;
            $('dumps-from').hidden = false;
        }

        var canvas = $('view');
        canvas.width = DESIGN_W;
        canvas.height = DESIGN_H;

        try {
            state.view = root.view.create(canvas);
        } catch (e) {
            fail('WebGL2', e);
            return;
        }
        $('renderer').textContent = state.view.renderer();

        /* params.bin is optional -- a scene can be opened without one -- but
           its absence is reported, not hidden, because it is the only thing
           holding the packer to the C. */
        Promise.all([
            fetchText(ROOT + '/shaders/trace.glsl'),
            fetchText(DUMPS + '/' + state.name + '_scene.json'),
            fetchBuffer(DUMPS + '/' + state.name + '_params.bin')
                .catch(function () { return null; }),
            fetchBuffer(DUMPS + '/' + state.name + '_ref.bin')
                .catch(function () { return null; }),
            /* Both optional: only the three examples with a walker dump a
               world, and the selftest arrives with them. */
            fetchText(DUMPS + '/' + state.name + '_walk.json')
                .then(JSON.parse).catch(function () { return null; }),
            fetchText(DUMPS + '/walk_selftest.json')
                .then(JSON.parse).catch(function () { return null; }),
            fetchText(DUMPS + '/' + state.name + '_pick.json')
                .then(JSON.parse).catch(function () { return null; }),
            /* Optional and scene-independent: whatever tools/bench last
               measured on this machine, if it was asked to write it down. */
            fetchText(DUMPS + '/bench.json')
                .then(JSON.parse).catch(function () { return null; })
        ]).then(function (loaded) {
            var shaderSrc = loaded[0], json = loaded[1], golden = loaded[2];

            state.shaderSource = shaderSrc;
            state.ref = loaded[3];
            state.benchDoc = loaded[7];
            /* Parsed twice, deliberately: the bench edits `doc` in place, and
               the oracle panel needs the scene the CPU reference was rendered
               from. One parse shared between them would let an edit quietly
               change what the diff claims to be checking. */
            state.pristine = JSON.parse(json);
            state.doc = JSON.parse(json);
            if (state.doc.format !== 'hologram/scene/1') {
                throw new Error('unknown scene format "' + state.doc.format + '"');
            }
            state.spectral = !!state.doc.spectral;
            $('spectral').checked = state.spectral;

            /* Two copies, for the same reason the scene has two: the trace
               belongs to the world it was recorded from, and the walls are
               about to become editable. state.walkDoc is what you edit and
               walk in; the conformance check keeps its own. */
            state.walkPristine = loaded[4];
            state.walkDoc = loaded[4]
                ? JSON.parse(JSON.stringify(loaded[4])) : null;

            state.cam = root.camera.create(state.doc);
            state.cam.setWorld(state.walkDoc);
            $('walk-row').hidden = !state.cam.canWalk();
            $('walk-row').classList.add('off');
            showWalkConformance(state.walkPristine, loaded[5]);
            /* Wired only once the camera exists: every handler reaches for
               it, and a keypress during the fetch would otherwise land on
               null. */
            wireInput(canvas);

            var fov = state.cam.state().fovDeg;
            $('fov').value = fov;
            $('fov-value').textContent = fov.toFixed(0) + '°';

            state.view.setShader(shaderSrc);

            /* The picker is a copy of the engine's intersections, so it is
               held to a grid the engine cast through this same camera.
               Against the pristine scene, for the reason every other check
               here uses the pristine one. */
            if (loaded[6]) {
                var pc = root.pick.conformance(state.pristine, loaded[6]);
                var el = $('pick-check');
                el.textContent = 'PICK ' + (pc.ok ? 'OK' : 'FAIL') + ': ' +
                    pc.agree + '/' + pc.total + ' rays agree with the engine' +
                    (pc.differ ? ', ' + pc.pct.toFixed(3) + '% differ' : '');
                el.className = 'note ' + (pc.ok ? 'ok' : 'bad');
            }

            showBudget();
            /* Conformance is judged on the scene AS DUMPED, before any edit:
               it is a check on the packer, not on the scene. Editing changes
               what is rendered, not whether this code packs it the way C
               does, so the verdict deliberately does not move afterwards. */
            showConformance(golden ? runConformance(golden) : null);

            state.bench = root.bench.create({
                doc: state.doc,
                ids: {
                    list: 'list', selTitle: 'sel-title', selBody: 'sel-body',
                    selTools: 'sel-tools', emit: 'emit',
                    emitToggle: 'emit-toggle', emitCopy: 'emit-copy',
                    undo: 'undo', redo: 'redo', dirty: 'dirty'
                },
                camera: function () { return state.cam; },
                changed: function () {
                    showBudget();
                    markDetached();
                    scheduleDraft();
                    invalidate();
                },
                selected: function () {
                    if (state.sweep) { state.sweep.render(); }
                },
                /* The walk world rides the same undo stack; see bench.js. */
                extra: {
                    get: function () {
                        return state.walkDoc ? state.walkDoc.world : null;
                    },
                    set: function (w) {
                        if (state.walkDoc && w) {
                            state.walkDoc.world = w;
                            state.cam.setWorld(state.walkDoc);
                        }
                    }
                },
                restored: function () {
                    if (state.plan) { state.plan.refresh(false); }
                    markWorldEdited();
                }
            });

            state.drag = root.drag.create({
                canvas: 'view',
                doc: function () { return state.doc; },
                camera: function () { return state.cam; },
                aspect: function () { return DESIGN_W / DESIGN_H; },
                select: function (s) { state.bench.setSelection(s); },
                beginEdit: function () { state.bench.beginWorldStroke(); },
                commit: function () { state.bench.commitWorldStroke(); },
                edited: function () {
                    state.bench.refreshEdited();
                    showBudget();
                    scheduleDraft();
                    invalidate();
                },
                changed: invalidate
            });

            state.plan = root.planview.create({
                canvas: 'plan', list: 'plan-list', body: 'plan-body',
                doc: function () { return state.doc; },
                walkDoc: function () { return state.walkDoc; },
                camera: function () { return state.cam; },
                beginEdit: function () { state.bench.beginWorldStroke(); },
                commit: function () { state.bench.commitWorldStroke(); },
                changed: function () {
                    /* Walls change nothing the tracer draws -- they are not
                       optical -- so the view is not repainted here. What
                       changes is where you can stand, which is why the world
                       is handed straight back to the walker. */
                    state.cam.setWorld(state.walkDoc);
                    markWorldEdited();
                    scheduleDraft();
                }
            });
            $('plan-section').hidden = !state.walkDoc;
            state.plan.refresh(true);

            state.benchPanel = root.costpanel.create({
                host: 'cost',
                doc: function () { return state.doc; },
                camera: function () { return state.cam; },
                shaderSource: function () { return state.shaderSource; },
                width: function () { return DESIGN_W; },
                height: function () { return DESIGN_H; },
                /* tools/bench's own numbers, when they have been produced.
                   The panel shows them beside its own and says which is
                   which; it never mixes them. */
                benchDoc: function () { return state.benchDoc; }
            });

            state.files = root.files.create({
                doc: function () { return state.doc; },
                camera: function () { return state.cam; },
                aspect: function () { return DESIGN_W / DESIGN_H; },
                origin: function () { return state.name; },
                suggestedName: function () { return state.name + '.scene.json'; }
            });
            if (!state.files.supported) {
                status('this browser has no file picker, so Save writes to ' +
                       'your downloads folder');
            }

            /* A draft from a previous visit. Restored rather than offered,
               because losing work quietly is the failure worth avoiding --
               and it is marked edited, so every check that depends on the
               dump says so. `discard` puts the dumped scene back. */
            var draft = root.save.readDraft(state.name);
            if (draft && draft.doc) {
                state.bench.replace(draft.doc);
                state.bench.markDirty();
                $('draft-row').hidden = false;
                $('draft-when').textContent =
                    new Date(draft.at).toLocaleString();
            }
            markDetached();

            state.sweep = root.sweeppanel.create({
                host: 'sweep',
                overlay: 'probe',
                doc: function () { return state.doc; },
                camera: function () { return state.cam; },
                spectral: function () { return state.spectral; },
                shaderSource: function () { return state.shaderSource; },
                selection: function () {
                    return state.bench ? state.bench.selection() : null;
                },
                width: function () { return DESIGN_W; },
                height: function () { return DESIGN_H; },
                /* A sweep restores the value it moved, so the live view is
                   showing the pre-sweep scene again and must be repainted. */
                onDone: function () { invalidate(); }
            });
            state.sweep.render();

            state.oracle = root.oraclepanel.create({
                host: 'oracle',
                name: state.name,
                doc: function () { return state.doc; },
                pristine: function () { return state.pristine; },
                shaderSource: function () { return state.shaderSource; },
                refBuffer: function () { return state.ref; },
                isDirty: function () {
                    return state.bench ? state.bench.isDirty() : false;
                }
            });

            state.running = true;
            invalidate();
        }).catch(function (e) { fail('load', e); });
    }

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', boot);
    } else {
        boot();
    }
}(window.Hologram = window.Hologram || {}));
