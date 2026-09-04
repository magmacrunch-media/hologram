/* The live view: shaders/trace.glsl, running in WebGL2.
 *
 * This is the engine's own tracer, not a preview of it. tools/gldiff proved
 * the arrangement -- "every browser ships a conformant GLSL ES 3.00 compiler
 * and a WebGL2 context, which is the same dialect sokol's GLES3 backend
 * feeds" -- and everything here is that page's machinery with a frame loop
 * around it instead of a single draw.
 *
 * The compile-error remapping is gldiff's and is worth keeping: the driver
 * numbers lines against the source it was handed, which includes a preamble
 * the file on disk does not have, so a raw error message points three lines
 * off from what you are editing.
 */
(function (root) {
    'use strict';

    /* display.c prepends exactly this for the GLES3 profile. */
    var PREAMBLE = '#version 300 es\nprecision highp float;\nprecision highp int;\n';
    var PREAMBLE_LINES = PREAMBLE.split('\n').length - 1;

    /* display.c's fullscreen triangle, GLSL spelling. No vertex buffer: the
       three corners come out of gl_VertexID. */
    var VS = PREAMBLE +
        'out vec2 uv;\n' +
        'void main() {\n' +
        '    vec2 grid = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n' +
        '    uv  = grid;\n' +
        '    gl_Position = vec4(grid * vec2(2.0, -2.0) + vec2(-1.0, 1.0), 0.0, 1.0);\n' +
        '}\n';

    function compileStage(gl, type, src, label) {
        var sh = gl.createShader(type);
        gl.shaderSource(sh, src);
        gl.compileShader(sh);
        if (gl.getShaderParameter(sh, gl.COMPILE_STATUS)) {
            return sh;
        }
        /* Report line numbers against the tracer file, not the preamble. */
        var lines = src.split('\n');
        var log = gl.getShaderInfoLog(sh) || '';
        var seen = {}, count = 0;
        var matches = log.matchAll(/ERROR:\s*\d+:(\d+)/g);
        for (var m of matches) {
            var n = parseInt(m[1], 10);
            if (seen[n] || count >= 12) {
                continue;
            }
            seen[n] = 1;
            count++;
            log += '\n  ' + label + ' line ' + (n - PREAMBLE_LINES) + ': ' +
                   (lines[n - 1] || '<eof>');
        }
        gl.deleteShader(sh);
        throw new Error(label + ' failed to compile:\n' + log);
    }

    /* A view over one canvas. `create` throws if there is no WebGL2 at all;
       `setShader` throws on a compile or link error, leaving any previously
       working program in place so a bad edit does not black out the view. */
    function create(canvas) {
        var gl = canvas.getContext('webgl2', {
            preserveDrawingBuffer: true,
            antialias: false
        });
        if (!gl) {
            throw new Error('this browser has no WebGL2 context');
        }

        var prog = null, loc = null;
        gl.bindVertexArray(gl.createVertexArray());

        function setShader(fsSource) {
            var vs = compileStage(gl, gl.VERTEX_SHADER, VS, 'vertex');
            var fs = compileStage(gl, gl.FRAGMENT_SHADER, PREAMBLE + fsSource,
                                  'trace.glsl');
            var p = gl.createProgram();
            gl.attachShader(p, vs);
            gl.attachShader(p, fs);
            gl.linkProgram(p);
            gl.deleteShader(vs);
            gl.deleteShader(fs);
            if (!gl.getProgramParameter(p, gl.LINK_STATUS)) {
                var log = gl.getProgramInfoLog(p);
                gl.deleteProgram(p);
                throw new Error('link failed:\n' + log);
            }
            var l = gl.getUniformLocation(p, 'params');
            if (l === null) {
                gl.deleteProgram(p);
                throw new Error("uniform 'params' was optimised out or renamed");
            }
            if (prog) {
                gl.deleteProgram(prog);
            }
            prog = p;
            loc = l;
            gl.useProgram(prog);
        }

        /* Draw one frame. `params` is the packed block; slot 0 is filled
           here, as display.c fills it every frame rather than trusting
           whatever the scene carried. */
        function draw(params, seconds) {
            if (!prog) {
                return;
            }
            var w = canvas.width, h = canvas.height;
            params[0] = w;
            params[1] = h;
            params[2] = seconds;
            params[3] = 0;
            gl.useProgram(prog);
            gl.uniform4fv(loc, params);
            gl.viewport(0, 0, w, h);
            gl.drawArrays(gl.TRIANGLES, 0, 3);
        }

        /* The frame, top row first -- glReadPixels hands back bottom-up, and
           display.c's GL readback applies the same flip. RGBA, w*h*4. */
        function readFrame() {
            var w = canvas.width, h = canvas.height;
            var px = new Uint8Array(w * h * 4);
            gl.readPixels(0, 0, w, h, gl.RGBA, gl.UNSIGNED_BYTE, px);
            var top = new Uint8Array(w * h * 4);
            for (var y = 0; y < h; y++) {
                top.set(px.subarray((h - 1 - y) * w * 4, (h - y) * w * 4), y * w * 4);
            }
            return { width: w, height: h, rgba: top };
        }

        return {
            gl: gl,
            setShader: setShader,
            draw: draw,
            readFrame: readFrame,
            renderer: function () { return gl.getParameter(gl.RENDERER); },
            hasProgram: function () { return prog !== null; }
        };
    }

    root.view = { create: create, PREAMBLE: PREAMBLE };
}(window.Hologram = window.Hologram || {}));
