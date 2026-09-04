/* Getting a scene out of the page and back into it.
 *
 * A page cannot write build/<name>_scene.json, and should not want to --
 * that file is what --dump produced and the references beside it belong to
 * it. What a page CAN do, given the File System Access API, is write a file
 * the person picked and keep the handle, so the first save asks where and
 * every save after it is silent. That is the whole of what the desktop
 * wrapper was wanted for here, and it turns out not to need one.
 *
 * Where the API is missing (Firefox, Safari) a save falls back to a
 * download, which lands in the downloads folder rather than where you asked
 * -- worse, but not nothing, and the difference is stated rather than
 * papered over.
 *
 * Independently of both, every edit writes a draft to localStorage, so a
 * reload or a closed tab does not lose work that was never saved to a file.
 * The draft is restored on load and the document is marked edited, which is
 * what makes the oracle panel go on saying its reference belongs to the
 * dump rather than to this.
 */
(function (root) {
    'use strict';

    var SUPPORTED = typeof window.showSaveFilePicker === 'function' &&
                    typeof window.showOpenFilePicker === 'function';

    var PICKER = {
        types: [{
            description: 'hologram scene',
            accept: { 'application/json': ['.json'] }
        }]
    };

    function create(opts) {
        var handle = null;         /* the file we are saving to, once chosen */
        var handleName = null;

        function text() {
            var doc = opts.doc();
            /* The camera as it is now: a save records where you were
               standing, which is most of why you would open it again. */
            doc.camera = root.save.cameraFrom(opts.camera(), opts.aspect());
            return root.save.toJson(doc, opts.origin());
        }

        function download(name, body) {
            var blob = new Blob([body], { type: 'application/json' });
            var url = URL.createObjectURL(blob);
            var a = document.createElement('a');
            a.href = url;
            a.download = name;
            document.body.appendChild(a);
            a.click();
            a.remove();
            /* Revoked on a timer rather than immediately: some browsers have
               not finished reading the blob when click() returns. */
            setTimeout(function () { URL.revokeObjectURL(url); }, 10000);
        }

        function writeTo(h, body) {
            return h.createWritable().then(function (w) {
                return w.write(body).then(function () { return w.close(); });
            });
        }

        /* Save to the file already chosen, or ask for one. `forceAsk` is
           Save As. Returns a promise resolving to a status string. */
        function save(forceAsk) {
            var body = text();

            if (!SUPPORTED) {
                download(opts.suggestedName(), body);
                return Promise.resolve('downloaded ' + opts.suggestedName() +
                    ' (this browser has no file picker, so it went to your ' +
                    'downloads folder)');
            }

            var got = (handle && !forceAsk)
                ? Promise.resolve(handle)
                : window.showSaveFilePicker(Object.assign({
                    suggestedName: opts.suggestedName()
                }, PICKER));

            return got.then(function (h) {
                return writeTo(h, body).then(function () {
                    handle = h;
                    handleName = h.name;
                    return 'saved ' + h.name;
                });
            });
        }

        function open() {
            if (!SUPPORTED) {
                return Promise.reject(new Error(
                    'this browser has no file picker; open a scene with ' +
                    '?s=<name> from build/ instead'));
            }
            return window.showOpenFilePicker(PICKER).then(function (picked) {
                var h = picked[0];
                return h.getFile().then(function (file) {
                    return file.text().then(function (body) {
                        var doc = JSON.parse(body);
                        if (doc.format !== 'hologram/scene/1') {
                            throw new Error('not a hologram scene: "' +
                                            doc.format + '"');
                        }
                        handle = h;
                        handleName = h.name;
                        return { doc: doc, name: h.name };
                    });
                });
            });
        }

        return {
            supported: SUPPORTED,
            save: save,
            open: open,
            fileName: function () { return handleName; },
            hasHandle: function () { return handle !== null; }
        };
    }

    root.files = { create: create, SUPPORTED: SUPPORTED };
}(window.Hologram = window.Hologram || {}));
