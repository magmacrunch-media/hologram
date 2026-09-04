/* The engine's hard limits, and what happens when a scene passes one.
 *
 * These are not style guidance. Each is a fixed-size array in cpu_trace.h or
 * a scalar slot in gpu_scene.h, and going past one does not raise an error
 * anywhere -- the primitive is silently not drawn, or worse, is drawn by the
 * CPU oracle and not by the GPU, which is a scene that passes its own --diff
 * on the machine that built it and is wrong on the machine that ships it.
 *
 * crystal-mirror-maze's first hall is at 24 of 24 rects. This is the number
 * a room designer needs on screen, which is most of why the editor exists.
 *
 * A dumped scene carries its own caps (scene_json.c writes them from the
 * macros), so these are the fallback for a scene that does not -- and the
 * place the consequence of each limit is written down.
 */
(function (root) {
    'use strict';

    var DEFAULTS = {
        spheres: 8,
        rects: 24,
        dishes: 4,
        gpu_gratings: 2,
        bounce: 16,
        rays: 32
    };

    var MEANING = {
        spheres: 'HOLO_MAX_SPHERES. Spheres past this are not traced at all.',
        rects: 'HOLO_MAX_RECTS. Panels past this are not traced at all.',
        dishes: 'HOLO_MAX_DISHES. Dishes past this are not traced at all.',
        gpu_gratings:
            'Gratings live in two scalar uniform slots, not an array, because ' +
            'fxc corrupts a dynamically indexed one. A third grating renders ' +
            'MATTE BLACK on the GPU while the CPU oracle renders it correctly ' +
            '-- so the scene looks right in a CPU render and is wrong in the game.',
        bounce: 'HOLO_MAX_BOUNCE. The walk returns what it has and stops.',
        rays: 'HOLO_MAX_RAYS. Split branches past this are dropped, ' +
              'identically on CPU and GPU so the oracle diff still holds.'
    };

    /* Counts against limits, with the gratings counted the way the GPU
       counts them: rects carrying a period, not rects in total. */
    function budget(doc) {
        var caps = doc && doc.caps ? doc.caps : DEFAULTS;
        var rects = (doc && doc.rects) || [];
        var gratings = rects.filter(function (r) {
            return r.grating_period > 0;
        }).length;

        function row(key, used, cap) {
            return {
                key: key,
                used: used,
                cap: cap,
                free: cap - used,
                over: used > cap,
                full: used >= cap,
                meaning: MEANING[key]
            };
        }

        return [
            row('rects', rects.length, caps.rects),
            row('spheres', ((doc && doc.spheres) || []).length, caps.spheres),
            row('dishes', ((doc && doc.dishes) || []).length, caps.dishes),
            row('gpu_gratings', gratings, caps.gpu_gratings)
        ];
    }

    root.caps = { DEFAULTS: DEFAULTS, MEANING: MEANING, budget: budget };
}(window.Hologram = window.Hologram || {}));
