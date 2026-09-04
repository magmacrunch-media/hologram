/* The oracle panel: GPU, CPU, and their difference, side by side.
 *
 * This is tools/gldiff/gldiff.html, absorbed. That page exists because the
 * GL tracer often cannot be run where it is written -- a Windows machine has
 * no GL toolchain, and D3D11 is the only backend that reads frames back --
 * so the tracer is rendered in a browser and held to a CPU frame the oracle
 * dumped. All of that is still true; what changes is that the browser is
 * already open, the scene is already loaded, and the shader is already
 * compiled.
 *
 * WHAT IT DIFFS, AND WHAT IT DOES NOT
 *
 * The scene AS DUMPED, always -- its camera, its spectral flag, its
 * resolution -- never the scene you have been editing. The reference frame
 * is a CPU render of the dumped scene and there is no CPU tracer here to
 * make another one. Rendering the edited scene against the old reference
 * would produce a confident number about two different scenes.
 *
 * Writing a JavaScript cpu_trace.c to lift that restriction would be a sixth
 * statement of the tracer, 578 lines of walk and stack and Fresnel, checked
 * by nothing. The engine already keeps four and pays for them with the
 * oracle diff. So: edit freely, and understand that this panel answers
 * "does trace.glsl agree with cpu_trace.c", which is a question about the
 * shader and not about your scene. It says so on its face when the scene has
 * been edited.
 */
(function (root) {
    'use strict';

    function el(tag, cls, text) {
        var e = document.createElement(tag);
        if (cls) { e.className = cls; }
        if (text !== undefined) { e.textContent = text; }
        return e;
    }

    function paint(canvas, w, h, rgba) {
        canvas.width = w;
        canvas.height = h;
        canvas.getContext('2d').putImageData(new ImageData(rgba, w, h), 0, 0);
    }

    /* opts: { root, doc, shaderSource(), refBuffer(), isDirty() } */
    function create(opts) {
        var host = document.getElementById(opts.host);
        var offscreen = null, offview = null;

        function fail(msg) {
            host.innerHTML = '';
            var p = el('p', 'bad', msg);
            host.appendChild(p);
        }

        function run() {
            var buffer = opts.refBuffer();
            if (!buffer) {
                fail('no reference for this scene -- run  build\\' +
                     opts.name + '.exe --dump');
                return;
            }

            var ref;
            try {
                ref = root.oracle.parseRef(buffer);
            } catch (e) {
                fail(e.message);
                return;
            }

            /* A second context at the reference's size, kept for reuse. The
               live view is left alone: resizing it would disturb what is
               being edited, and the reference is not always 640x480. */
            if (!offscreen || offscreen.width !== ref.width ||
                offscreen.height !== ref.height) {
                offscreen = document.createElement('canvas');
                offscreen.width = ref.width;
                offscreen.height = ref.height;
                try {
                    offview = root.view.create(offscreen);
                    offview.setShader(opts.shaderSource());
                } catch (e) {
                    fail('oracle view: ' + e.message);
                    return;
                }
            }

            /* The camera AS DUMPED -- a basis, not the flown one. */
            var doc = opts.doc();
            var c = doc.camera;
            var cam = {
                pos: root.linalg.fromArray(c.pos),
                forward: root.linalg.fromArray(c.forward),
                right: root.linalg.fromArray(c.right),
                up: root.linalg.fromArray(c.up),
                tan_half_fov: c.tan_half_fov,
                aspect: c.aspect
            };
            var params = root.scene.pack(opts.pristine(), cam, doc.spectral);

            /* time is not read by the tracer, but display.c writes the real
               framebuffer size into slot 0 every frame and the shader
               letterboxes from it, so the draw must be told the reference's
               size and not the live view's. */
            offview.draw(params, 0);
            var frame = offview.readFrame();

            var d;
            try {
                d = root.oracle.diff(frame, ref);
            } catch (e) {
                fail(e.message);
                return;
            }

            render(d);
        }

        function render(d) {
            host.innerHTML = '';

            var verdict = el('p', 'verdict ' + (d.ok ? 'ok' : 'bad'),
                             root.oracle.line(d));
            host.appendChild(verdict);

            host.appendChild(el('p', 'note',
                'bars: mean < ' + d.meanBar.toFixed(4) + '/255 and outliers < ' +
                d.outlierBar.toFixed(3) + '%'));

            if (opts.isDirty()) {
                host.appendChild(el('p', 'warnline',
                    'The scene has been edited. This diff is of the scene as ' +
                    'dumped -- the CPU reference is a render of that one, and ' +
                    'there is no CPU tracer here to make another.'));
            }

            /* Only worth saying when it is actually true of this frame. */
            if (d.meansDiverge) {
                host.appendChild(el('p', 'note',
                    'gldiff.html would report mean ' + d.meanGldiff.toFixed(4) +
                    '/255 for this frame: it sums every channel of an outlier ' +
                    'pixel where oracle.c stops at the offending one. ' +
                    "oracle.c's number is the verdict above."));
            }

            var strip = el('div', 'strip');
            [['GPU: trace.glsl in WebGL2', d.images.gpu],
             ['CPU: the oracle', d.images.cpu],
             ['difference, x8', d.images.diff]].forEach(function (pair) {
                var fig = el('figure');
                var cv = el('canvas');
                paint(cv, d.width, d.height, pair[1]);
                fig.appendChild(cv);
                fig.appendChild(el('figcaption', null, pair[0]));
                strip.appendChild(fig);
            });
            host.appendChild(strip);
        }

        /* Drop the compiled copy, so the next run picks up a reloaded
           tracer rather than diffing the one from before the edit -- which
           would be the most misleading result this panel could give. */
        function reset() {
            offscreen = null;
            offview = null;
        }

        return { run: run, reset: reset };
    }

    root.oraclepanel = { create: create };
}(window.Hologram = window.Hologram || {}));
