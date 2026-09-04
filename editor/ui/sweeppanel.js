/* The optics bench: put a detector somewhere, turn one knob, plot what
 * comes out.
 *
 * The probe is a rectangle drawn over the view; the sweep steps one field of
 * the selected primitive across a range, rendering each step offscreen at
 * the live camera and resolution so what is measured is what is on screen.
 * The plot is the measurement. Nothing here knows Malus's law -- turn a
 * polarizer through 180 degrees with a detector behind it and the law is
 * what the curve does.
 *
 * Rendering happens on its own view rather than the live one so the scene
 * being edited is never left mid-sweep if something throws, and the loop
 * yields between frames: forty-eight spectral renders at 640x480 is enough
 * work to freeze a tab, and a progress line costs nothing.
 */
(function (root) {
    'use strict';

    var SVG_NS = 'http://www.w3.org/2000/svg';

    function el(tag, cls, text) {
        var e = document.createElement(tag);
        if (cls) { e.className = cls; }
        if (text !== undefined) { e.textContent = text; }
        return e;
    }

    function svg(tag, attrs) {
        var e = document.createElementNS(SVG_NS, tag);
        Object.keys(attrs || {}).forEach(function (k) {
            e.setAttribute(k, attrs[k]);
        });
        return e;
    }

    /* A plain line plot. Deliberately unstyled beyond the page's own colours:
       it is a readout, not a chart library. */
    function plot(points, opts) {
        var W = 460, H = 200, L = 52, R = 10, T = 12, B = 30;
        var g = svg('svg', { viewBox: '0 0 ' + W + ' ' + H, class: 'plot' });
        if (points.length < 2) { return g; }

        var xs = points.map(function (p) { return p.v; });
        var ys = points.map(function (p) { return p.mean; });
        var x0 = Math.min.apply(null, xs), x1 = Math.max.apply(null, xs);
        var y1 = Math.max.apply(null, ys);
        var y0 = 0;                      /* intensity: the floor is zero */
        if (y1 <= y0) { y1 = y0 + 1e-9; }

        var px = function (v) { return L + (v - x0) / (x1 - x0) * (W - L - R); };
        var py = function (v) { return H - B - (v - y0) / (y1 - y0) * (H - T - B); };

        g.appendChild(svg('line', { x1: L, y1: H - B, x2: W - R, y2: H - B,
                                    class: 'axis' }));
        g.appendChild(svg('line', { x1: L, y1: T, x2: L, y2: H - B,
                                    class: 'axis' }));

        /* Four x ticks and three y, labelled; more is noise at this size. */
        for (var i = 0; i <= 4; i++) {
            var xv = x0 + (x1 - x0) * i / 4;
            var t = svg('text', { x: px(xv), y: H - B + 13, class: 'tick',
                                  'text-anchor': 'middle' });
            t.textContent = (+xv.toFixed(3)).toString();
            g.appendChild(t);
        }
        for (var j = 0; j <= 2; j++) {
            var yv = y0 + (y1 - y0) * j / 2;
            var ty = svg('text', { x: L - 5, y: py(yv) + 3, class: 'tick',
                                   'text-anchor': 'end' });
            ty.textContent = yv.toExponential(1);
            g.appendChild(ty);
        }

        var d = points.map(function (p, k) {
            return (k ? 'L' : 'M') + px(p.v).toFixed(2) + ' ' + py(p.mean).toFixed(2);
        }).join(' ');
        g.appendChild(svg('path', { d: d, class: 'curve' }));
        points.forEach(function (p) {
            g.appendChild(svg('circle', { cx: px(p.v), cy: py(p.mean), r: 1.6,
                                          class: 'dot' }));
        });

        var xl = svg('text', { x: (L + W - R) / 2, y: H - 2, class: 'axlabel',
                               'text-anchor': 'middle' });
        xl.textContent = opts.xLabel;
        g.appendChild(xl);
        var yl = svg('text', { x: 10, y: (T + H - B) / 2, class: 'axlabel',
                               'text-anchor': 'middle',
                               transform: 'rotate(-90 10 ' + ((T + H - B) / 2) + ')' });
        yl.textContent = 'mean linear luminance';
        g.appendChild(yl);
        return g;
    }

    /* opts: { host, overlay, doc, camera, spectral, shaderSource,
               selection, width, height, onProbeChange } */
    function create(opts) {
        var host = document.getElementById(opts.host);
        var overlay = document.getElementById(opts.overlay);
        var probe = { x: 288, y: 208, w: 64, h: 64 };
        var offscreen = null, offview = null;
        var lastPoints = null, lastLabel = '';
        var running = false;

        function drawOverlay() {
            var W = opts.width(), H = opts.height();
            overlay.style.left = (100 * probe.x / W) + '%';
            overlay.style.top = (100 * probe.y / H) + '%';
            overlay.style.width = (100 * probe.w / W) + '%';
            overlay.style.height = (100 * probe.h / H) + '%';
            overlay.hidden = false;
        }

        function ensureView() {
            var W = opts.width(), H = opts.height();
            if (offscreen && offscreen.width === W && offscreen.height === H) {
                return true;
            }
            offscreen = document.createElement('canvas');
            offscreen.width = W;
            offscreen.height = H;
            offview = root.view.create(offscreen);
            offview.setShader(opts.shaderSource());
            return true;
        }

        /* Drop the compiled copy so a reloaded tracer is picked up. */
        function reset() { offscreen = null; offview = null; }

        function numberInput(value, step, onInput) {
            var n = el('input');
            n.type = 'number';
            n.className = 'fnum';
            n.step = step;
            n.value = value;
            n.addEventListener('input', function () {
                var v = parseFloat(n.value);
                if (!isNaN(v)) { onInput(v); }
            });
            return n;
        }

        function render() {
            host.innerHTML = '';
            var sel = opts.selection();

            if (!sel || sel.scene) {
                host.appendChild(el('p', 'note',
                    'Select a sphere, rect or dish to sweep one of its fields.'));
                overlay.hidden = true;
                return;
            }
            drawOverlay();

            var spec = root.schema.LISTS.filter(function (s) {
                return s.key === sel.list;
            })[0];
            var obj = opts.doc()[sel.list][sel.index];
            var list = root.sweep.targets(spec.fields, obj);

            /* --- what to sweep --- */
            var row = el('div', 'srow');
            row.appendChild(el('label', 'flabel', 'sweep'));
            var pick = el('select', 'fsel');
            list.forEach(function (t, i) {
                var o = el('option', null, t.label + (t.inert ? '  (' + t.inert + ')' : ''));
                o.value = i;
                pick.appendChild(o);
            });
            row.appendChild(pick);
            host.appendChild(row);

            var target = list[0];
            var from = el('span'), to = el('span'), steps = el('span');
            var range = { from: target.from, to: target.to, steps: 48 };

            var rangeRow = el('div', 'srow');
            rangeRow.appendChild(el('label', 'flabel', 'from / to / steps'));
            var fromIn = numberInput(range.from, 0.01, function (v) { range.from = v; });
            var toIn = numberInput(range.to, 0.01, function (v) { range.to = v; });
            var stepIn = numberInput(range.steps, 1, function (v) {
                range.steps = Math.max(2, Math.min(256, Math.round(v)));
            });
            rangeRow.appendChild(fromIn);
            rangeRow.appendChild(toIn);
            rangeRow.appendChild(stepIn);
            host.appendChild(rangeRow);

            pick.addEventListener('change', function () {
                target = list[parseInt(pick.value, 10)];
                range.from = target.from;
                range.to = target.to;
                fromIn.value = range.from;
                toIn.value = range.to;
            });

            /* --- where the detector is --- */
            var pr = el('div', 'srow');
            pr.appendChild(el('label', 'flabel', 'probe x y w h'));
            ['x', 'y', 'w', 'h'].forEach(function (k) {
                pr.appendChild(numberInput(probe[k], 1, function (v) {
                    probe[k] = v;
                    drawOverlay();
                }));
            });
            host.appendChild(pr);

            /* --- go --- */
            var go = el('button', null, 'run sweep');
            var status = el('span', 'note', '');
            var bar = el('div', 'srow');
            bar.appendChild(go);
            bar.appendChild(status);
            host.appendChild(bar);

            var results = el('div');
            host.appendChild(results);

            go.addEventListener('click', function () {
                if (running) { return; }
                run(target, range, status, results, go);
            });

            if (lastPoints) { showResults(results, lastPoints, lastLabel); }
        }

        function run(target, range, status, results, go) {
            running = true;
            go.disabled = true;
            results.innerHTML = '';

            try {
                ensureView();
            } catch (e) {
                status.textContent = 'sweep view: ' + e.message;
                running = false;
                go.disabled = false;
                return;
            }

            var doc = opts.doc();
            var original = target.get();
            var points = [];
            var n = range.steps;
            var i = 0;

            function stepOnce() {
                if (i >= n) {
                    target.set(original);        /* always put it back */
                    lastPoints = points;
                    lastLabel = target.label;
                    status.textContent = n + ' steps';
                    showResults(results, points, target.label);
                    running = false;
                    go.disabled = false;
                    opts.onDone();
                    return;
                }
                var v = n === 1 ? range.from
                                : range.from + (range.to - range.from) * i / (n - 1);
                target.set(v);
                var params = root.scene.pack(doc, opts.camera().basis(
                    opts.width() / opts.height()), opts.spectral());
                offview.draw(params, 0);
                var m = root.sweep.measure(offview.readFrame(), probe);
                points.push({ v: v, mean: m.mean, min: m.min, max: m.max });
                i++;
                status.textContent = i + ' / ' + n;
                /* Yield, so the progress line actually paints. */
                setTimeout(stepOnce, 0);
            }

            status.textContent = '0 / ' + n;
            setTimeout(stepOnce, 0);
        }

        function showResults(into, points, label) {
            into.innerHTML = '';
            var s = root.sweep.summarise(points);
            if (!s) { return; }

            into.appendChild(plot(points, { xLabel: label }));

            var t = el('table', 'sstats');
            [['max', s.max.toExponential(3) + '  at ' + (+s.maxAt.toFixed(4))],
             ['min', s.min.toExponential(3) + '  at ' + (+s.minAt.toFixed(4))],
             ['extinction', s.extinction === Infinity
                 ? 'infinite (the minimum reads a true zero)'
                 : s.extinction.toFixed(1) + ' : 1']
            ].forEach(function (pair) {
                var tr = el('tr');
                tr.appendChild(el('td', null, pair[0]));
                tr.appendChild(el('td', 'snum', pair[1]));
                t.appendChild(tr);
            });
            into.appendChild(t);

            var copy = el('button', null, 'copy CSV');
            copy.addEventListener('click', function () {
                var csv = root.sweep.toCsv(points, label);
                navigator.clipboard.writeText(csv).then(function () {
                    copy.textContent = 'copied';
                    setTimeout(function () { copy.textContent = 'copy CSV'; }, 1200);
                }).catch(function () { copy.textContent = 'clipboard refused'; });
            });
            into.appendChild(copy);
        }

        return { render: render, reset: reset, probe: function () { return probe; } };
    }

    root.sweeppanel = { create: create };
}(window.Hologram = window.Hologram || {}));
