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
        var doc = state.doc;
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
        return !!(state.held.w || state.held.a || state.held.s ||
                  state.held.d || state.held.q || state.held.e);
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

        if (moving() && dt > 0) {
            var speed = (state.held.shift ? 8 : 2.5) * dt;
            state.cam.move((state.held.d ? 1 : 0) - (state.held.a ? 1 : 0),
                           (state.held.e ? 1 : 0) - (state.held.q ? 1 : 0),
                           (state.held.w ? 1 : 0) - (state.held.s ? 1 : 0),
                           speed);
            state.fps = dt > 0 ? state.fps * 0.9 + (1 / dt) * 0.1 : state.fps;
        }

        var basis = state.cam.basis(DESIGN_W / DESIGN_H);
        state.params = root.scene.pack(state.doc, basis, state.spectral);
        state.view.draw(state.params, now / 1000);

        var s = state.cam.state();
        $('readout').textContent =
            'pos ' + s.pos.x.toFixed(2) + ' ' + s.pos.y.toFixed(2) + ' ' +
            s.pos.z.toFixed(2) +
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
        });
        window.addEventListener('keyup', function (e) {
            if (KEYS[e.code]) {
                state.held[KEYS[e.code]] = false;
            }
            if (e.code === 'ShiftLeft' || e.code === 'ShiftRight') {
                state.held.shift = false;
            }
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

        canvas.addEventListener('click', function () {
            if (document.pointerLockElement !== canvas) {
                canvas.requestPointerLock();
            }
        });
        document.addEventListener('mousemove', function (e) {
            if (document.pointerLockElement === canvas) {
                state.cam.look(e.movementX * 0.0025, e.movementY * 0.0025);
                invalidate();
            }
        });
        document.addEventListener('pointerlockchange', function () {
            var locked = document.pointerLockElement === canvas;
            $('lock-hint').textContent = locked
                ? 'Esc releases the mouse'
                : 'click the view to look around';
            if (!locked) {
                state.held = {};
            }
        });

        $('spectral').addEventListener('change', function (e) {
            state.spectral = e.target.checked;
            invalidate();
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
        state.name = new URLSearchParams(location.search).get('s') || 'm7_room';
        $('scene-name').textContent = state.name;
        document.title = 'hologram — ' + state.name;

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
            fetchText(ROOT + '/build/' + state.name + '_scene.json'),
            fetchBuffer(ROOT + '/build/' + state.name + '_params.bin')
                .catch(function () { return null; }),
            fetchBuffer(ROOT + '/build/' + state.name + '_ref.bin')
                .catch(function () { return null; })
        ]).then(function (loaded) {
            var shaderSrc = loaded[0], json = loaded[1], golden = loaded[2];

            state.shaderSource = shaderSrc;
            state.ref = loaded[3];
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

            state.cam = root.camera.create(state.doc);
            /* Wired only once the camera exists: every handler reaches for
               it, and a keypress during the fetch would otherwise land on
               null. */
            wireInput(canvas);

            var fov = state.cam.state().fovDeg;
            $('fov').value = fov;
            $('fov-value').textContent = fov.toFixed(0) + '°';

            state.view.setShader(shaderSrc);

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
                    invalidate();
                },
                selected: function () {
                    if (state.sweep) { state.sweep.render(); }
                }
            });

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
