/* What a primitive's fields ARE: name, kind, range, and what they mean.
 *
 * The inspector is generated from these tables and knows nothing about
 * spheres. Adding a material field to HoloRect is a line here, not new UI --
 * which is the only way a hand-written editor stays in step with an engine
 * that is still growing.
 *
 * The help text is lifted from source/cpu_trace.h, deliberately and close to
 * verbatim. Those comments are where the material model is actually
 * explained -- that a rect's glass is a thin pane and a sphere's is a
 * volume, that Fresnel splits the transmit share by angle, that gratings
 * ignore the glass fields entirely -- and a person tuning a scene needs that
 * in front of them, not one directory away.
 *
 * `inert(obj)` marks a field the engine will ignore given the rest of the
 * primitive: ior on something with no transmit, retard on a polarizer,
 * grating_angle with no period. Inert fields are shown greyed with the
 * reason rather than hidden, because a field that vanishes reads as a
 * missing feature, and "ignored while transmit is 0" teaches the model.
 */
(function (root) {
    'use strict';

    var S = root.scene;

    /* Shared material fields. HoloSphere and HoloRect carry the same four
       glass fields with the same meanings; only the geometry differs. */
    function glassFields(kind) {
        var pane = kind === 'rect';
        return [
            { key: 'transmit', kind: 'unit',
              help: pane
                  ? 'Glass as a thin pane: direction unchanged, one Fresnel ' +
                    'interface. A window, not a prism.'
                  : 'Glass as a volume: rays bend in and out and can be ' +
                    'trapped by total internal reflection.' },
            { key: 'ior', kind: 'float', min: 1, max: 3, step: 0.001,
              help: 'Refractive index at the sodium D line. BK7 is 1.5168; ' +
                    'dense flint runs past 1.7.',
              inert: function (o) {
                  return !(o.transmit > 0) && 'ignored while transmit is 0';
              } },
            { key: 'disperse', kind: 'float', min: 0, max: 0.1, step: 0.0005,
              label: 'disperse (Cauchy B)',
              help: 'Cauchy B in um^2. 0 is achromatic glass; BK7 is about ' +
                    '0.0042. n(D) equals ior for every B, so this only ' +
                    'spreads the colours apart.',
              inert: function (o) {
                  return !(o.transmit > 0) && 'ignored while transmit is 0';
              } }
        ];
    }

    var MIRROR = {
        key: 'mirror', kind: 'unit',
        help: 'Metallic reflection, tinted by albedo -- silver is a colour ' +
              'too. mirror + transmit must not exceed 1; the remainder is ' +
              'matte Lambert.'
    };

    var ALBEDO = {
        key: 'albedo', kind: 'color',
        help: 'Diffuse colour, and the tint of the mirror share.'
    };

    var SPHERE = [
        { key: 'center', kind: 'vec3', unit: 'm' },
        { key: 'radius', kind: 'float', min: 0.01, max: 20, step: 0.01,
          unit: 'm' },
        ALBEDO,
        MIRROR
    ].concat(glassFields('sphere'));

    var FILTER_OPTIONS = [
        { value: S.FILTER_NONE, label: 'none', c: 'HOLO_FILTER_NONE' },
        { value: S.POLARIZER, label: 'polarizer', c: 'HOLO_POLARIZER' },
        { value: S.WAVEPLATE, label: 'waveplate', c: 'HOLO_WAVEPLATE' }
    ];

    function notAFilter(o) {
        return !(o.filter > 0) && 'ignored: this panel is not a filter';
    }

    var RECT = [
        { key: 'corner', kind: 'vec3', unit: 'm' },
        { key: 'edge_u', kind: 'vec3', unit: 'm',
          help: "One edge of the panel. Its LENGTH is the panel's size -- " +
                'edges need not be perpendicular; the intersection solves a ' +
                'parallelogram.' },
        { key: 'edge_v', kind: 'vec3', unit: 'm',
          help: 'The other edge. filter_angle and grating_angle are measured ' +
                'from edge_u toward edge_v.' },
        ALBEDO,
        MIRROR
    ].concat(glassFields('rect'), [
        { key: 'filter', kind: 'enum', options: FILTER_OPTIONS,
          help: 'An ideal optical filter INSTEAD of glass. A filter ignores ' +
                'mirror, transmit and ior entirely. Polarization physics ' +
                'lives in the spectral path; the RGB path approximates a ' +
                'polarizer as a flat 50% absorber.' },
        { key: 'filter_angle', kind: 'angle',
          help: 'The axis, measured from edge_u toward edge_v. A polarizer ' +
                'passes the component along it and Malus does the rest.',
          inert: notAFilter },
        { key: 'retard', kind: 'angle', max: 720,
          help: 'Waveplate retardance at the D line: how far p is retarded ' +
                'against s about the axis. Scales as 1/lambda the way a ' +
                'zero-order plate does, which is why a thick plate between ' +
                'crossed polarizers shows interference colours.',
          inert: function (o) {
              return o.filter !== S.WAVEPLATE && 'only a waveplate retards';
          } },
        { key: 'grating_period', kind: 'float', min: 0, max: 10, step: 0.01,
          unit: 'um', label: 'grating period',
          help: 'Groove spacing in micrometres; 0 is not a grating. 1.2 um ' +
                "is a spectroscopist's 833 lines/mm. A grating ignores the " +
                'glass and filter fields, and the RGB path shows only the ' +
                'zeroth order.' },
        { key: 'grating_angle', kind: 'angle',
          help: 'Which way the grooves run, from edge_u toward edge_v.',
          inert: function (o) {
              return !(o.grating_period > 0) && 'ignored: period is 0';
          } },
        { key: 'order_w', kind: 'vec4', labels: ['m=-1', 'm=0', 'm=+1', 'm=+2'],
          label: 'order weights',
          help: 'Fixed efficiency per order: both first orders for the ' +
                'symmetric spectra, the second for order overlap, and the ' +
                'zeroth is specular. Hand-set scalars -- rigorous efficiency ' +
                "is a solver's job, not a renderer's.",
          inert: function (o) {
              return !(o.grating_period > 0) && 'ignored: period is 0';
          } }
    ]);

    var DISH = [
        { key: 'apex', kind: 'vec3', unit: 'm' },
        { key: 'axis', kind: 'vec3', normalize: true,
          help: 'Unit, pointing out of the bowl.' },
        { key: 'curv_r', kind: 'float', min: 0.1, max: 50, step: 0.05,
          unit: 'm', label: 'vertex radius R',
          help: 'Radius of curvature at the vertex. A paraboloid focuses at ' +
                'R/2 -- put the sun disk on and you can see it happen.' },
        { key: 'conic_k', kind: 'float', min: -3, max: 1, step: 0.01,
          label: 'conic constant K',
          help: '0 sphere, -1 paraboloid, < -1 hyperboloid, between -1 and 0 ' +
                'a prolate ellipsoid that images one focus onto the other.' },
        { key: 'rim', kind: 'float', min: 0.05, max: 20, step: 0.05, unit: 'm',
          help: 'Half-aperture: the dish is cut off beyond this radius.' },
        ALBEDO,
        MIRROR
    ];

    /* Scene-level fields, in two groups, matching HoloScene's own ordering. */
    var FLOOR = [
        { key: 'has_floor', kind: 'bool' },
        { key: 'floor_y', kind: 'float', min: -20, max: 20, step: 0.05,
          unit: 'm' },
        { key: 'floor_a', kind: 'color', help: 'One square of the 1m checker.' },
        { key: 'floor_b', kind: 'color', help: 'The other.' },
        { key: 'floor_mirror', kind: 'unit',
          help: 'A polished floor reflects a little.' }
    ];

    var SKY = [
        { key: 'sun_dir', kind: 'vec3', normalize: true,
          help: 'Unit, pointing from the scene toward the sun.' },
        { key: 'horizon', kind: 'color' },
        { key: 'zenith', kind: 'color' },
        { key: 'sun_disk_cos', kind: 'float', min: 0.9, max: 1, step: 0.0001,
          help: 'Rays within acos() of sun_dir see the disk instead of the ' +
                'gradient. 0.9995 is roughly the real sun.' },
        { key: 'sun_disk_intensity', kind: 'float', min: 0, max: 200, step: 1,
          help: 'Zero turns the disk off, which is the default. This is what ' +
                "makes focusing visible: at a paraboloid's focus every point " +
                'of the dish shows you the sun.' }
    ];

    /* What a newly added primitive is. Deliberately visible and neutral --
       a matte white thing in front of the camera, not something invisible
       that has to be hunted for. */
    var DEFAULTS = {
        sphere: function () {
            return { center: [0, 1, 0], radius: 0.5, albedo: [0.8, 0.8, 0.8],
                     mirror: 0, transmit: 0, ior: 0, disperse: 0 };
        },
        rect: function () {
            return { corner: [-1, 0, 0], edge_u: [2, 0, 0], edge_v: [0, 2, 0],
                     albedo: [0.8, 0.8, 0.8], mirror: 0, transmit: 0, ior: 0,
                     disperse: 0, filter: 0, filter_angle: 0, retard: 0,
                     grating_period: 0, grating_angle: 0,
                     order_w: [0, 0, 0, 0] };
        },
        dish: function () {
            return { apex: [0, 0, 0], axis: [0, 0, 1], curv_r: 4, conic_k: -1,
                     rim: 1, albedo: [0.9, 0.9, 0.9], mirror: 0.9 };
        }
    };

    var LISTS = [
        { key: 'spheres', kind: 'sphere', label: 'sphere', cap: 'spheres',
          fields: SPHERE },
        { key: 'rects', kind: 'rect', label: 'rect', cap: 'rects',
          fields: RECT },
        { key: 'dishes', kind: 'dish', label: 'dish', cap: 'dishes',
          fields: DISH }
    ];

    /* A one-line description for the list, so a row says what it is without
       being opened: "mirror", "flint glass", "grating 1.2um", "polarizer". */
    function describe(kind, o) {
        if (kind === 'rect') {
            if (o.grating_period > 0) {
                return 'grating ' + o.grating_period + 'um';
            }
            if (o.filter === S.POLARIZER) { return 'polarizer'; }
            if (o.filter === S.WAVEPLATE) { return 'waveplate'; }
        }
        if (o.transmit > 0) {
            return o.disperse > 0 ? 'glass n=' + o.ior + ' dispersive'
                                  : 'glass n=' + o.ior;
        }
        if (o.mirror >= 0.99) { return 'mirror'; }
        if (o.mirror > 0) { return 'part mirror ' + o.mirror; }
        return 'matte';
    }

    root.schema = {
        SPHERE: SPHERE, RECT: RECT, DISH: DISH,
        FLOOR: FLOOR, SKY: SKY,
        LISTS: LISTS, DEFAULTS: DEFAULTS,
        FILTER_OPTIONS: FILTER_OPTIONS,
        describe: describe
    };
}(window.Hologram = window.Hologram || {}));
