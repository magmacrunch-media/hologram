/* Timing the tracer in the page, on its own offscreen surface.
 *
 * The measurement is GPU timestamps where the browser offers them
 * (EXT_disjoint_timer_query_webgl2) and nothing at all where it does not --
 * see core/bench.js for why a frame clock is not a substitute and is
 * labelled differently when it has to serve.
 *
 * A disjoint result is thrown away rather than averaged in. The extension
 * reports that flag when the GPU was interrupted mid-query -- another
 * context took the device, the machine slept -- and the elapsed time it
 * returns for such a frame is meaningless rather than merely noisy.
 */
(function (root) {
    'use strict';

    /* bench.c takes --frames for the same reason this is adjustable: more
       frames is a steadier median and a longer wait, and which you want
       depends on whether you are chasing a 5% regression or glancing at a
       room. Warmup is proportional and never zero -- the first frames after
       a shader or a scene change are not representative of either. */
    var DEFAULT_SAMPLES = 40;

    function el(tag, cls, text) {
        var e = document.createElement(tag);
        if (cls) { e.className = cls; }
        if (text !== undefined) { e.textContent = text; }
        return e;
    }

    function create(opts) {
        var host = document.getElementById(opts.host);
        var canvas = null, view = null, gl = null, ext = null;
        var running = false;
        var last = null;

        function ensure() {
            if (view) { return true; }
            canvas = document.createElement('canvas');
            canvas.width = opts.width();
            canvas.height = opts.height();
            view = root.view.create(canvas);
            view.setShader(opts.shaderSource());
            gl = view.gl;
            ext = gl.getExtension('EXT_disjoint_timer_query_webgl2');
            return true;
        }

        function reset() { canvas = null; view = null; gl = null; ext = null; }

        /* Polling for a query result is not frame-paced work and must not be
           tied to requestAnimationFrame: a backgrounded tab stops issuing
           those and the run simply hangs, half measured, with no error to
           show for it. A timeout still throttles in the background, but it
           progresses. */
        function soon() {
            return new Promise(function (r) { setTimeout(r, 0); });
        }

        /* Draw n frames back to back, each wrapped in its own timer query,
           and only afterwards collect what they measured.
         *
           Polling for each result before issuing the next draw -- the
           obvious way to write this -- puts a stall between every frame,
           which is both slow and a different workload from the one being
           measured. bench.c runs its frames continuously for the same
           reason. Only one TIME_ELAPSED query may be active at a time, so
           they are still strictly sequential; what changes is that nothing
           waits on a result until every draw has been issued. */
        function timedRun(params, n) {
            var queries = [];
            for (var i = 0; i < n; i++) {
                var q = gl.createQuery();
                gl.beginQuery(ext.TIME_ELAPSED_EXT, q);
                view.draw(params, 0);
                gl.endQuery(ext.TIME_ELAPSED_EXT);
                queries.push(q);
            }
            var deadline = performance.now() + 4000;
            return new Promise(function (resolve) {
                (function collect() {
                    if (gl.getParameter(ext.GPU_DISJOINT_EXT)) {
                        queries.forEach(function (q) { gl.deleteQuery(q); });
                        resolve({ ms: [], disjoint: true });
                        return;
                    }
                    var lastQ = queries[queries.length - 1];
                    var ready = gl.getQueryParameter(lastQ,
                                                     gl.QUERY_RESULT_AVAILABLE);
                    if (ready) {
                        var out = queries.map(function (q) {
                            var ms = gl.getQueryParameter(q, gl.QUERY_RESULT) / 1e6;
                            gl.deleteQuery(q);
                            return ms;
                        });
                        resolve({ ms: out, disjoint: false });
                        return;
                    }
                    if (performance.now() > deadline) {
                        queries.forEach(function (q) { gl.deleteQuery(q); });
                        resolve({ ms: [], timedOut: true });
                        return;
                    }
                    soon().then(collect);
                }());
            });
        }

        /* One timed draw. Resolves to milliseconds, or null when the sample
           has to be discarded. */
        function timeOnce(params) {
            if (!ext) {
                /* No GPU clock. readPixels forces the pipeline to drain, so
                   this at least waits for the work instead of timing the
                   loop -- but it also times the readback, and it is reported
                   under a different name for both reasons. */
                var t0 = performance.now();
                view.draw(params, 0);
                view.readFrame();
                return Promise.resolve(performance.now() - t0);
            }
            var q = gl.createQuery();
            gl.beginQuery(ext.TIME_ELAPSED_EXT, q);
            view.draw(params, 0);
            gl.endQuery(ext.TIME_ELAPSED_EXT);

            /* Bounded in WALL TIME, not in polls. A backgrounded tab is not
               driving the GPU, so its queries never become available -- and
               throttled timeouts turn "600 tries" into ten minutes of
               looking stuck. A second is far longer than any frame here. */
            var deadline = performance.now() + 1000;
            return new Promise(function (resolve) {
                (function poll() {
                    var done = gl.getQueryParameter(q, gl.QUERY_RESULT_AVAILABLE);
                    var disjoint = gl.getParameter(ext.GPU_DISJOINT_EXT);
                    if (disjoint) {
                        gl.deleteQuery(q);
                        resolve(null);          /* the GPU was interrupted */
                        return;
                    }
                    if (done) {
                        var ns = gl.getQueryParameter(q, gl.QUERY_RESULT);
                        gl.deleteQuery(q);
                        resolve(ns / 1e6);
                        return;
                    }
                    if (performance.now() > deadline) {
                        gl.deleteQuery(q);
                        resolve(undefined);     /* never answered */
                        return;
                    }
                    soon().then(poll);
                }());
            });
        }

        function measure(doc, spectral, status, label, n) {
            var warmup = Math.max(3, Math.round(n * 0.3));
            var params = root.scene.pack(doc, opts.camera().basis(
                opts.width() / opts.height()), spectral);
            status.textContent = label + '…';

            if (!ext) {
                /* No GPU clock: one at a time is all the fallback can do,
                   since it times a draw-plus-readback per sample. */
                var samples = [];
                var i = 0;
                return (function step() {
                    if (i >= warmup + n) {
                        return Promise.resolve({
                            ms: root.cost.median(samples),
                            samples: samples.length, discarded: 0
                        });
                    }
                    return timeOnce(params).then(function (ms) {
                        if (typeof ms === 'number' && i >= warmup) {
                            samples.push(ms);
                        }
                        i++;
                        return step();
                    });
                }());
            }

            return timedRun(params, warmup + n).then(function (r) {
                if (r.timedOut) {
                    return { ms: 0, samples: 0, discarded: 0, abandoned: true };
                }
                if (r.disjoint) {
                    return { ms: 0, samples: 0, discarded: warmup + n };
                }
                /* Warmup frames are dropped after the fact rather than timed
                   separately: the first frames after a scene change are not
                   representative, and issuing them in the same run keeps the
                   GPU busy the way the measured ones find it. */
                var kept = r.ms.slice(warmup);
                return { ms: root.cost.median(kept),
                         spread: root.cost.spread(kept),
                         samples: kept.length, discarded: 0 };
            });
        }

        function run(status, frames) {
            running = true;
            ensure();
            var n = frames || DEFAULT_SAMPLES;
            var doc = opts.doc();
            var count = (doc.rects || []).length;
            var ladder = root.cost.stages(count);
            var out = { stages: [], gpuClock: !!ext, frames: n };

            var chain = Promise.resolve();
            ladder.forEach(function (panels) {
                chain = chain.then(function () {
                    return measure(root.cost.truncated(doc, panels), true,
                                   status, panels + ' panels', n);
                }).then(function (r) {
                    out.stages.push({ panels: panels, ms: r.ms,
                                      spread: r.spread,
                                      discarded: r.discarded });
                    if (r.abandoned) { out.abandoned = true; }
                });
            });

            /* The whole scene both ways. Spectral is twelve traces where RGB
               is one, and it is the largest single lever in the room. */
            chain = chain.then(function () {
                return measure(doc, true, status, 'spectral', n);
            }).then(function (r) { out.spectral = r.ms; })
              .then(function () {
                return measure(doc, false, status, 'rgb', n);
            }).then(function (r) { out.rgb = r.ms; });

            return chain.then(function () {
                var base = out.stages.length ? out.stages[0].ms : 0;
                out.stages.forEach(function (s) {
                    s.vs1 = base > 0 ? s.ms / base : 0;
                });
                running = false;
                last = out;
                return out;
            }).catch(function (e) {
                running = false;
                throw e;
            });
        }

        function render(result) {
            host.innerHTML = '';
            var theirs = opts.benchDoc();

            var method = el('p', 'note',
                result.gpuClock
                    ? 'GPU timestamps, median of ' + result.frames +
                      ' frames, ' + opts.width() + 'x' + opts.height()
                    : 'NO GPU CLOCK in this browser: these are draw-plus-' +
                      'readback times, not shader times. Compare them to ' +
                      'each other, never to a backend that timestamps.');
            if (!result.gpuClock) { method.className = 'note bad'; }
            host.appendChild(method);

            if (result.abandoned) {
                var warn = el('p', 'note bad',
                    'The GPU stopped answering timer queries part-way -- ' +
                    'usually a backgrounded tab, which is not running this ' +
                    "page's work at all. The numbers below are incomplete. " +
                    'Bring the window to the front and measure again.');
                host.appendChild(warn);
            }

            var d = root.cost.decompose(result.stages);

            host.appendChild(el('p', 'note',
                'everything that is not a panel: ' + d.fixed.toFixed(3) +
                ' ms  (spheres, dishes, floor, sky, and the wavelengths), ' +
                'noise +-' + d.noise.toFixed(3)));

            var t = el('table', 'benchtable');
            var head = el('tr');
            ['panels', 'ms', 'panels add', 'each'].forEach(function (h) {
                head.appendChild(el('th', null, h));
            });
            t.appendChild(head);
            d.rows.forEach(function (r) {
                var tr = el('tr');
                tr.appendChild(el('td', null, String(r.panels)));
                tr.appendChild(el('td', 'num', r.ms.toFixed(3)));
                if (r.belowNoise) {
                    var cell = el('td', 'num dim', 'below noise');
                    cell.colSpan = 2;
                    tr.appendChild(cell);
                } else {
                    tr.appendChild(el('td', 'num', r.added.toFixed(3)));
                    tr.appendChild(el('td', 'num', r.perPanel.toFixed(4)));
                }
                t.appendChild(tr);
            });
            host.appendChild(t);

            if (d.allBelowNoise) {
                host.appendChild(el('p', 'note',
                    'Every panel count measures the same as none of them: in ' +
                    'this scene, on this backend, the panels are not what the ' +
                    'frame is spending its time on. Look at the spectral row ' +
                    'below before looking at the panel budget.'));
            }

            if (result.spectral && result.rgb) {
                host.appendChild(el('p', 'note',
                    'whole scene: spectral ' + result.spectral.toFixed(3) +
                    ' ms, RGB ' + result.rgb.toFixed(3) + ' ms  (' +
                    (result.spectral / result.rgb).toFixed(2) +
                    'x for twelve wavelengths)'));
            }

            /* bench's numbers, under their own heading and never in the same
               table. It measures a synthetic scene of N panels and nothing
               else, on the backend a game ships; this measures a real room
               with everything else still in it, in WebGL2. Neither the
               milliseconds nor the ratios cross between them. */
            if (theirs) {
                host.appendChild(el('h3', 'subhead',
                    'tools/bench — ' + theirs.backend));
                var bt = el('table', 'benchtable');
                var bh = el('tr');
                ['panels', 'ms', 'vs 1'].forEach(function (h) {
                    bh.appendChild(el('th', null, h));
                });
                bt.appendChild(bh);
                theirs.stages.forEach(function (s) {
                    var tr = el('tr');
                    tr.appendChild(el('td', null, String(s.panels)));
                    tr.appendChild(el('td', 'num', s.ms.toFixed(3)));
                    tr.appendChild(el('td', 'num', s.vs1.toFixed(2) + 'x'));
                    bt.appendChild(tr);
                });
                host.appendChild(bt);
                host.appendChild(el('p', 'note',
                    theirs.timing + ', ' + theirs.width + 'x' + theirs.height +
                    ', median of ' + theirs.frames + '. A different scene as ' +
                    'well as a different backend: bench builds N panels and ' +
                    'nothing else, so neither its milliseconds nor its ratios ' +
                    'compare with the table above.'));
            } else {
                host.appendChild(el('p', 'note',
                    'For numbers from the backend a game ships on, run ' +
                    'build\\bench.exe --json and reload.'));
            }
            var discarded = result.stages.reduce(function (a, s) {
                return a + (s.discarded || 0);
            }, 0);
            if (discarded) {
                host.appendChild(el('p', 'note',
                    discarded + ' samples discarded as disjoint (the GPU was ' +
                    'interrupted mid-query).'));
            }
        }

        return {
            run: function (status, frames) {
                if (running) { return Promise.resolve(null); }
                return run(status, frames).then(function (r) {
                    render(r);
                    return r;
                });
            },
            reset: reset,
            last: function () { return last; }
        };
    }

    root.costpanel = { create: create };
}(window.Hologram = window.Hologram || {}));
